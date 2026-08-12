#!/usr/bin/env python3
"""
minisql.py  —  the SQL front-end for the Mini SQL engine.

Pipeline (this is the classic compiler structure):

    SQL string
       |  tokenizer      -> a flat list of tokens
       v
    tokens
       |  parser         -> an AST (Select/Insert/Create/... node objects)
       v
    AST
       |  compiler       -> bytecode (list of tab-separated instructions)
       v
    bytecode  --pipe-->  ./minisql  (the C++ virtual machine executes it)

Run it as a REPL:      python3 minisql.py
Run a .sql script:     python3 minisql.py script.sql
Just show bytecode:    python3 minisql.py --emit "SELECT * FROM users"
"""

import sys
import subprocess
import shlex
import os

HERE = os.path.dirname(os.path.abspath(__file__))
# On Windows the compiled binary is "minisql.exe"; on Linux/Mac it's "minisql".
# Prefer whichever one actually exists next to this script.
_exe = os.path.join(HERE, "minisql.exe")
_plain = os.path.join(HERE, "minisql")
ENGINE = _exe if os.path.exists(_exe) else _plain
DATA_DIR = os.path.join(HERE, "data")
UNIT  = "\x1f"   # separates col/op/value inside ONE condition
COND  = "\x1e"   # separates conditions inside one AND-group
GROUP = "\x1d"   # separates AND-groups (the groups are OR'd together)

# ---------------------------------------------------------------------------
# 1. TOKENIZER  — turn raw SQL text into a list of tokens
# ---------------------------------------------------------------------------
# A token is a keyword, identifier, number, string, operator or punctuation.
# We keep it small: enough SQL to cover the features the engine supports.

TWO_CHAR_OPS = {">=", "<=", "!=", "<>"}
ONE_CHAR = set("(),*=<>")

def tokenize(sql):
    tokens = []
    i, n = 0, len(sql)
    while i < n:
        c = sql[i]
        if c.isspace():
            i += 1
        elif c == "'" or c == '"':                 # quoted string literal
            quote = c
            j = i + 1
            while j < n and sql[j] != quote:
                j += 1
            tokens.append(("str", sql[i + 1:j]))
            i = j + 1
        elif sql[i:i + 2] in TWO_CHAR_OPS:         # two-char operator
            op = sql[i:i + 2]
            tokens.append(("op", "!=" if op == "<>" else op))
            i += 2
        elif c in ONE_CHAR:                          # single-char op / punct
            tokens.append(("op" if c in "=<>*" else "punct", c))
            i += 1
        else:                                        # word or number
            j = i
            while j < n and not sql[j].isspace() and sql[j] not in ONE_CHAR \
                    and sql[j] not in ("'", '"'):
                j += 1
            word = sql[i:j]
            if word.replace(".", "", 1).lstrip("-").isdigit():
                tokens.append(("num", word))
            else:
                tokens.append(("word", word))
            i = j
    return tokens

# ---------------------------------------------------------------------------
# 2. AST node types
# ---------------------------------------------------------------------------
class Create:  # CREATE TABLE
    def __init__(self, table, cols):        self.table, self.cols = table, cols
class Insert:
    def __init__(self, table, values):      self.table, self.values = table, values
class Select:
    def __init__(self, cols, table, where, join, group, agg, having, order):
        self.cols, self.table, self.where = cols, table, where
        self.join, self.group, self.agg = join, group, having  # placeholder
        self.group, self.agg, self.having, self.order = group, agg, having, order
class Update:
    def __init__(self, table, setcol, setval, where):
        self.table, self.setcol, self.setval, self.where = table, setcol, setval, where
class Delete:
    def __init__(self, table, where):       self.table, self.where = table, where

# ---------------------------------------------------------------------------
# 3. PARSER  — recursive-descent over the token list, producing an AST
# ---------------------------------------------------------------------------
class Parser:
    def __init__(self, tokens):
        self.t = tokens
        self.i = 0

    def peek(self):  return self.t[self.i] if self.i < len(self.t) else (None, None)
    def next(self):
        tok = self.peek(); self.i += 1; return tok
    def kw(self, word):                     # expect a keyword (case-insensitive)
        k, v = self.next()
        if v is None or v.upper() != word.upper():
            raise SyntaxError(f"expected {word!r}, got {v!r}")
    def eat_word(self):
        k, v = self.next()
        if k not in ("word", "num", "str"):
            raise SyntaxError(f"expected a name, got {v!r}")
        return v

    def parse(self):
        _, v = self.peek()
        head = (v or "").upper()
        if head == "CREATE": return self.p_create()
        if head == "INSERT": return self.p_insert()
        if head == "SELECT": return self.p_select()
        if head == "UPDATE": return self.p_update()
        if head == "DELETE": return self.p_delete()
        raise SyntaxError(f"unsupported statement: {v}")

    def p_create(self):
        self.kw("CREATE"); self.kw("TABLE")
        table = self.eat_word()
        self.kw("(")
        cols = []
        while True:
            name = self.eat_word()
            ctype = self.eat_word().upper()
            cols.append((name, "INT" if ctype == "INT" else "TEXT"))
            k, v = self.next()
            if v == ")": break
            if v != ",": raise SyntaxError("expected , or ) in column list")
        return Create(table, cols)

    def p_insert(self):
        self.kw("INSERT"); self.kw("INTO")
        table = self.eat_word()
        self.kw("VALUES"); self.kw("(")
        vals = []
        while True:
            vals.append(self.eat_word())
            k, v = self.next()
            if v == ")": break
            if v != ",": raise SyntaxError("expected , or ) in values")
        return Insert(table, vals)

    def _condition(self):
        # a single test: <col> <op> <value>, e.g.  age > 25
        col = self.eat_word()
        _, op = self.next()
        val = self.eat_word()
        return (col, op, val)

    def _and_group(self):
        # one or more conditions chained with AND: age > 25 AND dept = 10
        # ALL of these must be true for a row to match this group.
        conds = [self._condition()]
        while True:
            k, v = self.peek()
            if v and v.upper() == "AND":
                self.next()
                conds.append(self._condition())
            else:
                break
        return conds

    def _where_expr(self):
        # AND-groups chained with OR: (age > 25 AND dept = 10) OR name = Bob
        # A row matches if ANY ONE of the AND-groups matches in full.
        # This gives correct SQL precedence: AND binds tighter than OR.
        groups = [self._and_group()]
        while True:
            k, v = self.peek()
            if v and v.upper() == "OR":
                self.next()
                groups.append(self._and_group())
            else:
                break
        return groups

    def p_select(self):
        self.kw("SELECT")
        cols, agg = [], None
        # column list — may be "*", plain columns, or a single aggregate FUNC(col)
        while True:
            k, v = self.next()
            if v == "*":
                cols.append("*")
            elif v and v.upper() in ("COUNT", "SUM", "AVG", "MIN", "MAX"):
                self.kw("(")
                k2, v2 = self.next()          # column name or '*'
                acol = "*" if v2 == "*" else v2
                self.kw(")")
                agg = (v.upper(), acol)
            else:
                cols.append(v)
            k, v = self.peek()
            if v == ",": self.next(); continue
            break
        self.kw("FROM")
        table = self.eat_word()

        join = where = group = having = order = None
        while True:
            k, v = self.peek()
            if v is None: break
            u = v.upper()
            if u in ("INNER", "RIGHT", "JOIN"):
                jtype = "inner"
                if u in ("INNER", "RIGHT"): jtype = u.lower(); self.next(); self.kw("JOIN")
                else: self.next()
                jtable = self.eat_word()
                self.kw("ON")
                lc = self.eat_word(); self.next(); rc = self.eat_word()
                # strip table-qualified names a.x / b.y down to the column
                lc = lc.split(".")[-1]; rc2 = rc.split(".")[-1]
                join = (jtable, lc, rc2, jtype)
            elif u == "WHERE":
                self.next(); where = self._where_expr()
            elif u == "GROUP":
                self.next(); self.kw("BY"); group = self.eat_word()
            elif u == "HAVING":
                self.next(); _, op = self.next(); val = self.eat_word(); having = (op, val)
            elif u == "ORDER":
                self.next(); self.kw("BY"); ocol = self.eat_word()
                k, v = self.peek()
                odir = "asc"
                if v and v.upper() in ("ASC", "DESC"): odir = v.lower(); self.next()
                order = (ocol, odir)
            else:
                break
        return Select(cols, table, where, join, group, agg, having, order)

    def p_update(self):
        self.kw("UPDATE")
        table = self.eat_word()
        self.kw("SET")
        col = self.eat_word(); self.next(); val = self.eat_word()
        where = None
        k, v = self.peek()
        if v and v.upper() == "WHERE": self.next(); where = self._where_expr()
        return Update(table, col, val, where)

    def p_delete(self):
        self.kw("DELETE"); self.kw("FROM")
        table = self.eat_word()
        where = None
        k, v = self.peek()
        if v and v.upper() == "WHERE": self.next(); where = self._where_expr()
        return Delete(table, where)

# ---------------------------------------------------------------------------
# 4. COMPILER  — walk the AST and emit bytecode instructions
# ---------------------------------------------------------------------------
def encode_where(groups):
    """Pack a where-expression (list of AND-groups, OR'd together) into a
    single bytecode argument the C++ VM can parse back apart.

    groups looks like:  [ [(col,op,val), (col,op,val)], [(col,op,val)] ]
                           \\___ AND-group 0 ___/          \\_ AND-group 1 _/
    meaning:  group0_cond0 AND group0_cond1   OR   group1_cond0

    Separators (all non-printable, so they can never collide with real data):
        UNIT  (\\x1f) between col / op / value inside one condition
        COND  (\\x1e) between conditions inside one AND-group
        GROUP (\\x1d) between AND-groups (the groups are OR'd)
    """
    group_strs = []
    for group in groups:
        cond_strs = [UNIT.join([c, op, v]) for c, op, v in group]
        group_strs.append(COND.join(cond_strs))
    return GROUP.join(group_strs)

def is_trivial_condition(where):
    """True if `where` is just ONE condition with no AND/OR — the only shape
    simple enough to route through the B+ tree index (SEEK) instead of a
    full scan + filter."""
    return where and len(where) == 1 and len(where[0]) == 1

def compile_ast(node):
    out = []
    def emit(*parts): out.append("\t".join(parts))

    if isinstance(node, Create):
        cols = ",".join(f"{n}:{t}" for n, t in node.cols)
        emit("CREATE", node.table, cols)

    elif isinstance(node, Insert):
        emit("INSERT", node.table, UNIT.join(node.values))

    elif isinstance(node, Delete):
        emit("SCAN", node.table)
        if node.where:
            emit("FILTER", encode_where(node.where))
        emit("DELETE", node.table)

    elif isinstance(node, Update):
        emit("SCAN", node.table)
        if node.where:
            emit("FILTER", encode_where(node.where))
        emit("UPDATE", node.table, node.setcol, node.setval)

    elif isinstance(node, Select):
        # choose an index seek when filtering an INT column with a single
        # simple comparison and no AND/OR; otherwise a full scan + filter.
        # (The engine builds the B+ tree for SEEK.) WHERE with AND/OR always
        # needs to check every row's full expression, so it falls back to
        # SCAN + FILTER — that's a correctness requirement, not laziness.
        used_seek = False
        if node.where and not node.join and is_trivial_condition(node.where):
            c, op, v = node.where[0][0]
            if op in ("=", "<", "<=", ">", ">=") and v.lstrip("-").isdigit():
                emit("SEEK", node.table, c, op, v)
                used_seek = True
        if not used_seek:
            emit("SCAN", node.table)
            if node.where:
                emit("FILTER", encode_where(node.where))
        if node.join:
            jt, lc, rc, jtype = node.join
            emit("JOIN", jt, lc, rc, jtype)
        if node.agg:
            func, acol = node.agg
            alias = f"{func.lower()}_{acol}" if acol != "*" else f"{func.lower()}"
            emit("GROUP", node.group or "", func, acol, alias)
            if node.having:
                op, v = node.having
                emit("HAVING", op, v)
            if node.order:
                emit("ORDER", node.order[0], node.order[1])
        else:
            # IMPORTANT: sort BEFORE projecting columns away. ORDER BY is
            # allowed to reference a column that isn't in the SELECT list
            # (e.g. "SELECT name FROM users ORDER BY age") — if we projected
            # first, the "age" column would already be gone and the sort
            # would have nothing to sort by.
            if node.order:
                emit("ORDER", node.order[0], node.order[1])
            proj = "*" if (not node.cols or node.cols == ["*"]) else ",".join(node.cols)
            emit("PROJECT", proj)
        emit("OUTPUT")

    return "\n".join(out)

def sql_to_bytecode(sql):
    return compile_ast(Parser(tokenize(sql)).parse())

# ---------------------------------------------------------------------------
# 5. DRIVER  — compile each statement and hand the bytecode to the C++ VM
# ---------------------------------------------------------------------------
def run_bytecode(bytecode):
    if not os.path.exists(ENGINE):
        sys.exit("engine binary not found — run `make` first")
    p = subprocess.run([ENGINE, DATA_DIR], input=bytecode,
                       capture_output=True, text=True)
    sys.stdout.write(p.stdout)
    if p.stderr: sys.stderr.write(p.stderr)

def strip_comments(sql):
    # remove everything after -- on each line
    lines = []
    for line in sql.splitlines():
        pos = line.find("--")
        lines.append(line if pos < 0 else line[:pos])
    return "\n".join(lines)

def run_sql(sql):
    sql = strip_comments(sql)
    for stmt in [s.strip() for s in sql.split(";") if s.strip()]:
        try:
            bc = sql_to_bytecode(stmt)
        except SyntaxError as e:
            print(f"Parse error: {e}")
            continue
        run_bytecode(bc)

def repl():
    print("MiniSQL (Python front-end -> C++ VM).  End statements with ;  .exit to quit\n")
    buf = ""
    while True:
        try:
            line = input("minisql> " if not buf else "     ... ")
        except EOFError:
            break
        if line.strip() in (".exit", ".quit"):
            break
        buf += " " + line
        if ";" in buf:
            run_sql(buf)
            buf = ""
    print("bye")

def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--emit":
        print(sql_to_bytecode(sys.argv[2]))
    elif len(sys.argv) >= 2:
        with open(sys.argv[1]) as f:
            run_sql(f.read())
    else:
        repl()

if __name__ == "__main__":
    main()
