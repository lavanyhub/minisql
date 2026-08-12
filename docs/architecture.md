# Architecture — the full pipeline

This doc covers everything after Stage 1 (storage): the SQL compiler, the
bytecode, the virtual machine's operators, and the B+ tree index. Read
`stage1_storage.md` first for the on-disk format.

## The big idea: compiler front-end + execution back-end

We split the system exactly the way real databases do.

- **Front-end (`minisql.py`, Python):** turns SQL *text* into a *plan*.
  `tokenizer → parser → AST → bytecode`. It knows SQL grammar but nothing about
  how data is stored.
- **Back-end (`src/*.cpp`, C++):** a virtual machine that executes bytecode
  against the flat files. It knows storage, indexes, and relational operators
  but nothing about SQL syntax.

They communicate through a tiny text **bytecode** and the tables on disk. You
can see the bytecode for any query:

```
$ python3 minisql.py --emit "SELECT name FROM users WHERE age > 30"
SEEK    users   age     >       30
PROJECT name
OUTPUT
```

## 1. Tokenizer

`tokenize()` scans the raw string once, left to right, and emits a flat list of
tokens: keywords/identifiers (`word`), numbers (`num`), quoted strings (`str`),
operators (`op`) and punctuation. It handles two-char operators (`>=`, `<=`,
`!=`) before single-char ones so `>=` isn't read as `>` then `=`.

## 2. Parser (recursive descent)

`Parser` walks the token list with one method per statement type
(`p_select`, `p_insert`, …). "Recursive descent" just means the grammar is
expressed directly as functions that call each other and consume tokens. Each
returns a small **AST node** (`Select`, `Insert`, `Create`, `Update`,
`Delete`) holding the parsed pieces — columns, table, WHERE condition, JOIN,
GROUP BY, HAVING, ORDER BY.

This is where SQL's flexibility is absorbed into a fixed, predictable shape.

## 3. Compiler

`compile_ast()` walks an AST node and emits bytecode lines. Key decisions live
here — for example, **when to use the index**: if a `SELECT` filters an INT
column with a comparison, it emits `SEEK` (index path); otherwise `SCAN` +
`FILTER` (full-scan path). That choice is a miniature **query planner**.

## 4. Bytecode & the Virtual Machine

Bytecode is one instruction per line, tab-separated: `OPCODE arg1 arg2 ...`.
The VM (`vm.cpp`) runs a **pipeline over one current result set** (`cur_`).
Each opcode transforms it:

| Opcode | Meaning |
|--------|---------|
| `CREATE` / `INSERT` | create a table / append a typed, checked row |
| `SCAN` | load a whole table into `cur_` |
| `SEEK` | build a B+ tree on a column and probe it (index path) |
| `FILTER` | keep rows matching `col op value` |
| `PROJECT` | keep only selected columns (`*` = all) |
| `JOIN` | combine `cur_` with another table on a key |
| `GROUP` | collapse rows into `COUNT/SUM/AVG/MIN/MAX` per group |
| `HAVING` | filter grouped rows on the aggregate |
| `ORDER` | sort by a column asc/desc |
| `UPDATE` / `DELETE` | write changes back to the table file |
| `OUTPUT` | print `cur_` as an ASCII grid |

Because the VM never parses SQL, adding a SQL feature is usually "emit a new
opcode from the compiler" — the parser and executor stay decoupled.

### Typed comparison (`value.h`)

Rows are stored as strings. `compareTyped()` converts *lazily*: INT columns are
compared numerically (`"10" > "9"`), TEXT lexicographically. Every operator
(FILTER, JOIN key match, ORDER, HAVING) routes through it, so type behaviour is
consistent in one place.

### Row identity for UPDATE / DELETE

`SCAN` and `FILTER` carry each surviving row's original index (`rowIds`) and its
source table. `UPDATE`/`DELETE` use those ids to change exactly the right rows
in the base file, then re-save. `PROJECT`/`JOIN`/`GROUP` drop the ids because
after them a "row" no longer maps to one base-table row.

## 5. The B+ tree index (`bptree.h`)

A B+ tree maps an integer key → the list of row ids holding that key.

- **All data lives in the leaves**, which are linked left-to-right. Internal
  nodes only hold separator keys to guide the search.
- **Point lookup** `find(k)` descends from the root, O(log n).
- **Range scan** `range(lo, hi)` finds the first qualifying leaf then walks the
  leaf links — this is why a B+ tree beats a hash index for `BETWEEN`/`>`/`<`.
- **Splits**: when a node fills up (`ORDER-1` keys) it splits; a leaf copies its
  middle key up, an internal node moves it up. That keeps the tree balanced and
  shallow.

`ORDER` is small (4) here so splits are easy to observe; production trees use
hundreds so each node fills a disk page.

### Why not just a hash index?

The benchmark (`bench/bench.cpp`) makes the trade-off concrete over 200k rows:
linear scan ≈ 1015 ms, B+ tree ≈ 56 ms (**~18× faster**), hash ≈ 1.5 ms. Hash
wins on raw point-lookup speed but **cannot do ordered range queries**; the B+
tree does both, which is why real databases default to it for indexes.

## Interview talking points

- *"I separated the SQL compiler from the executor — Python tokenizes, parses to
  an AST, and compiles to a small bytecode; a C++ VM executes that bytecode.
  It's the same planner/executor split SQLite and Postgres use."*
- *"The compiler does a basic planning decision: integer comparisons compile to
  a B+ tree `SEEK`, everything else to a full `SCAN` + `FILTER`."*
- *"My B+ tree keeps data in linked leaves, so it does O(log n) point lookups
  and range scans — I benchmarked it ~18× faster than a linear scan, and I can
  explain exactly why I'd still pick it over a faster hash index (range
  queries)."*
- *"Values are stored as strings and typed lazily at comparison time, so INT
  columns compare numerically and TEXT lexicographically through one code path."*
- *"UPDATE and DELETE track original row ids through the pipeline so writes hit
  exactly the right rows in the flat file."*
