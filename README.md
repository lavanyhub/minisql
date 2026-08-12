# MiniSQL — a relational query engine built from scratch

![Build and Test](https://github.com/lavanyhub/minisql/actions/workflows/build.yml/badge.svg)

The badge above is not decoration — it's a live, automatic proof this project
actually compiles and runs correctly. Every time code is pushed, GitHub
builds the C++ engine from scratch and runs the full demo script against it.
If anything breaks, the badge turns red immediately. Click it to see the real
build logs.

A small but real database engine: you type SQL, it parses it into an AST,
compiles that to **bytecode**, and a **C++ virtual machine** executes the
bytecode against flat-file tables — using a hand-written **B+ tree** index for
fast lookups. Written to understand how production databases actually work
inside.

```
   SQL text
     │  tokenizer            (minisql.py)
     ▼
   tokens
     │  recursive-descent parser
     ▼
   AST
     │  compiler
     ▼
   bytecode  ──pipe──▶  ./minisql   (C++ VM: storage + B+ tree + operators)
                                    │
                                    ▼
                             flat-file tables (data/*.tbl)
```

The split is deliberate: **Python is the compiler front-end**, **C++ is the
execution engine**. Real databases (SQLite, Postgres) separate the same way —
a planner that turns SQL into an operation plan, and an executor that runs it.

## What it supports

- `CREATE TABLE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`
- `WHERE` with `=  !=  <  <=  >  >=` on INT (numeric) and TEXT (lexicographic)
- `WHERE ... AND ... OR ...` — full boolean expressions with correct SQL
  precedence (AND binds tighter than OR), e.g.
  `WHERE age > 30 AND dept = 10 OR name = 'Bob'`
- **B+ tree index seek** — automatically used for a single integer comparison;
  supports point lookups *and* range scans
- `INNER JOIN` and `RIGHT JOIN ... ON` — implemented as a **hash join**
  (O(n+m), not the naive O(n×m) nested loop)
- `GROUP BY` with `COUNT / SUM / AVG / MIN / MAX`, and `HAVING` — grouping
  uses a hash map, not a linear re-scan per row
- `ORDER BY ... ASC|DESC`
- Type checking on insert (an INT column rejects non-integers)

## Tests

This isn't just "it ran without crashing" — there's a real automated test
suite with actual pass/fail assertions:

- `tests/bptree_test.cpp` — unit tests for the B+ tree itself (point lookup,
  duplicate keys, range scans, and correctness after hundreds of node splits)
- `tests/test_engine.py` — end-to-end tests that check the *answers* are
  correct: WHERE/AND/OR, JOIN, GROUP BY/HAVING, ORDER BY, UPDATE, DELETE, and
  type-checking on INSERT

```bash
g++ -std=c++17 -O2 tests/bptree_test.cpp -I src -o tests/bptree_test && ./tests/bptree_test
python3 -m unittest discover -s tests -v
```

Both run automatically on every push — see the CI badge above.

## Requirements

- A C++17 compiler (`g++` or `clang++`)
- Python 3.8+ (**standard library only — no `pip install` needed**)
- `make` (optional convenience)

See `SETUP.md` for Windows/WSL instructions.

## Build & run

```bash
make                       # compiles the ./minisql engine
python3 minisql.py         # interactive SQL REPL

# or run the demo script:
python3 minisql.py examples/demo.sql

# see the bytecode a query compiles to:
python3 minisql.py --emit "SELECT dept, COUNT(*) FROM users GROUP BY dept"
```

Example session:

```sql
CREATE TABLE users (id INT, name TEXT, age INT, dept INT);
INSERT INTO users VALUES (1, Alice, 30, 10);
SELECT name, age FROM users WHERE age > 30;
SELECT dept, AVG(age) FROM users GROUP BY dept HAVING > 30;
SELECT * FROM users INNER JOIN depts ON dept = did;
```

## Benchmark

`bench/bench.cpp` compares point-lookup strategies over 200k rows:

| Method       | Time (20k lookups) | Notes |
|--------------|--------------------|-------|
| Linear scan  | ~1015 ms           | O(n) per lookup |
| **B+ tree**  | ~56 ms (**18× faster**) | O(log n); also does range scans |
| Hash map     | ~1.5 ms            | O(1) point, but **no range queries** |

```bash
g++ -std=c++17 -O2 bench/bench.cpp -I src -o bench/bench && ./bench/bench
```

The point: the B+ tree gives near-hash lookup speed *and* keeps keys ordered so
`WHERE age BETWEEN x AND y` works — a hash index can't do that.

## Layout

```
src/
  table.h / table.cpp   storage layer: schema, rows, flat-file save/load, typing
  value.h               typed comparison (INT numeric vs TEXT lexicographic)
  bptree.h              B+ tree index (point find + range scan)
  vm.h / vm.cpp         bytecode virtual machine (all query operators)
  main.cpp              engine entry point (reads bytecode from stdin)
minisql.py              SQL front-end: tokenizer + parser + compiler + REPL
bench/bench.cpp         index vs scan vs hash benchmark
examples/demo.sql       end-to-end feature demo
docs/                   per-stage explanations + interview notes
```

## Docs

Each build stage is written up in `docs/` with how-it-works detail and
interview talking points:

- `docs/stage1_storage.md` — storage layer & on-disk format
- `docs/architecture.md` — the full pipeline, bytecode, B+ tree, operators
