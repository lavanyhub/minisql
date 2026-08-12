#!/usr/bin/env python3
"""
tests/test_engine.py — real automated tests for MiniSQL.

Unlike a "smoke test" (which only checks that the program doesn't crash),
these tests check that the ANSWERS are actually correct: run a query,
compare the real output against the exact output we expect. If the engine's
logic breaks, a test here fails — a crash is not required to catch a bug.

Run it:
    python3 -m unittest discover -s tests -v

Each test gets its own throwaway data directory (via tempfile), so tests
never touch your real data/ folder and can't interfere with each other.
"""
import os
import sys
import shutil
import tempfile
import subprocess
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
import minisql as m  # noqa: E402  (path insert must happen first)


def run(sql, data_dir):
    """Compile `sql` (may be several ; separated statements) and run it
    against a fresh engine process, returning everything it printed."""
    out = []
    for stmt in [s.strip() for s in m.strip_comments(sql).split(";") if s.strip()]:
        bytecode = m.sql_to_bytecode(stmt)
        p = subprocess.run([m.ENGINE, data_dir], input=bytecode,
                            capture_output=True, text=True)
        out.append(p.stdout)
    return "\n".join(out)


class EngineTestCase(unittest.TestCase):
    """Base class: gives every test a fresh, empty data directory."""

    def setUp(self):
        if not os.path.exists(m.ENGINE):
            self.skipTest(f"engine binary not built yet: {m.ENGINE}")
        self.data_dir = tempfile.mkdtemp(prefix="minisql_test_")

    def tearDown(self):
        shutil.rmtree(self.data_dir, ignore_errors=True)

    def run_sql(self, sql):
        return run(sql, self.data_dir)

    def seed_users(self):
        """A small, fixed dataset every test can rely on."""
        self.run_sql("""
            CREATE TABLE users (id INT, name TEXT, age INT, dept INT);
            INSERT INTO users VALUES (1, Alice, 30, 10);
            INSERT INTO users VALUES (2, Bob, 25, 20);
            INSERT INTO users VALUES (3, Carol, 40, 10);
            INSERT INTO users VALUES (4, Dave, 35, 20);
            INSERT INTO users VALUES (5, Eve, 28, 10);
        """)


class TestCreateInsertSelect(EngineTestCase):
    def test_create_and_insert_then_select_returns_all_rows(self):
        self.seed_users()
        out = self.run_sql("SELECT * FROM users;")
        self.assertIn("5 row(s).", out)
        for name in ("Alice", "Bob", "Carol", "Dave", "Eve"):
            self.assertIn(name, out)

    def test_insert_rejects_bad_int_and_row_is_not_added(self):
        self.seed_users()
        out = self.run_sql("INSERT INTO users VALUES (six, Frank, 22, 10);")
        self.assertIn("Error", out)
        # the bad row must NOT have been written
        out2 = self.run_sql("SELECT * FROM users;")
        self.assertIn("5 row(s).", out2)
        self.assertNotIn("Frank", out2)


class TestWhere(EngineTestCase):
    def test_simple_greater_than_uses_index_and_correct_rows(self):
        self.seed_users()
        out = self.run_sql("SELECT name FROM users WHERE age > 30;")
        self.assertIn("[index] B+ tree seek", out)   # proves it took the fast path
        self.assertIn("Carol", out)
        self.assertIn("Dave", out)
        self.assertNotIn("Alice", out)
        self.assertNotIn("Bob", out)
        self.assertNotIn("Eve", out)

    def test_and_condition_requires_both_to_be_true(self):
        self.seed_users()
        out = self.run_sql("SELECT name FROM users WHERE age > 25 AND dept = 10;")
        # age>25 AND dept=10 -> Alice(30,10), Carol(40,10), Eve(28,10)
        for name in ("Alice", "Carol", "Eve"):
            self.assertIn(name, out)
        for name in ("Bob", "Dave"):
            self.assertNotIn(name, out)
        self.assertIn("3 row(s).", out)

    def test_or_condition_matches_either_side(self):
        self.seed_users()
        out = self.run_sql("SELECT name FROM users WHERE name = Bob OR name = Dave;")
        self.assertIn("Bob", out)
        self.assertIn("Dave", out)
        self.assertIn("2 row(s).", out)

    def test_and_binds_tighter_than_or(self):
        self.seed_users()
        # "age > 30 AND dept = 10  OR  name = Bob" must mean
        # (age>30 AND dept=10) OR (name=Bob)  ->  Carol, Bob
        out = self.run_sql("SELECT name FROM users WHERE age > 30 AND dept = 10 OR name = Bob;")
        self.assertIn("Carol", out)
        self.assertIn("Bob", out)
        self.assertIn("2 row(s).", out)
        for name in ("Alice", "Dave", "Eve"):
            self.assertNotIn(name, out)


class TestJoin(EngineTestCase):
    def test_inner_join_pairs_matching_rows(self):
        self.seed_users()
        self.run_sql("""
            CREATE TABLE depts (did INT, dname TEXT);
            INSERT INTO depts VALUES (10, Engineering);
            INSERT INTO depts VALUES (20, Sales);
        """)
        out = self.run_sql("SELECT * FROM users INNER JOIN depts ON dept = did;")
        self.assertIn("5 row(s).", out)          # every user has a matching dept
        self.assertIn("Engineering", out)
        self.assertIn("Sales", out)


class TestGroupByHaving(EngineTestCase):
    def test_count_per_group(self):
        self.seed_users()
        out = self.run_sql("SELECT dept, COUNT(*) FROM users GROUP BY dept;")
        # dept 10 has 3 users (Alice, Carol, Eve), dept 20 has 2 (Bob, Dave)
        self.assertIn("3", out)
        self.assertIn("2", out)
        self.assertIn("2 row(s).", out)          # 2 distinct groups

    def test_having_filters_groups_on_the_aggregate(self):
        self.seed_users()
        out = self.run_sql("SELECT dept, AVG(age) FROM users GROUP BY dept HAVING > 30;")
        # avg(dept 10) = (30+40+28)/3 = 32.67 -> passes HAVING > 30
        # avg(dept 20) = (25+35)/2   = 30     -> fails HAVING > 30
        self.assertIn("1 row(s).", out)
        self.assertIn("10", out)


class TestOrderBy(EngineTestCase):
    def test_order_by_desc_sorts_correctly(self):
        self.seed_users()
        out = self.run_sql("SELECT name FROM users ORDER BY age DESC;")
        # expected order by age descending: Carol(40) Dave(35) Alice(30) Eve(28) Bob(25)
        expected_order = ["Carol", "Dave", "Alice", "Eve", "Bob"]
        positions = [out.index(name) for name in expected_order]
        self.assertEqual(positions, sorted(positions),
                          "rows were not sorted in descending age order")


class TestUpdateDelete(EngineTestCase):
    def test_update_changes_only_matching_rows(self):
        self.seed_users()
        self.run_sql("UPDATE users SET age = 99 WHERE name = Carol;")
        out = self.run_sql("SELECT name, age FROM users WHERE name = Carol;")
        self.assertIn("99", out)
        out2 = self.run_sql("SELECT age FROM users WHERE name = Alice;")
        self.assertNotIn("99", out2)             # unrelated row untouched

    def test_delete_removes_only_matching_rows(self):
        self.seed_users()
        self.run_sql("DELETE FROM users WHERE age < 28;")   # removes Bob(25)
        out = self.run_sql("SELECT * FROM users;")
        self.assertIn("4 row(s).", out)
        self.assertNotIn("Bob", out)
        self.assertIn("Alice", out)               # everyone else stays


if __name__ == "__main__":
    unittest.main()
