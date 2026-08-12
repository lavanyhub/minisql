# Stage 1 — Storage Layer + CLI

This stage builds the foundation every later feature sits on: how a table is
represented in memory, how it is written to and read from disk, and a REPL to
drive it. No indexing, no WHERE, no JOIN yet — just correct, typed storage.

## Files

| File | Role |
|------|------|
| `src/table.h`   | Declares `Column`, `Schema`, `Row`, and the `Table` class. |
| `src/table.cpp` | Implements storage: type checking, save/load to a flat file. |
| `src/main.cpp`  | A temporary REPL that hand-parses CREATE / INSERT / SELECT. |
| `Makefile`      | `make`, `make run`, `make clean`. |

## Data model

A table is three things: a **name**, a **schema** (ordered list of typed
columns), and a **vector of rows**. A row is stored as a `vector<string>` — one
string per column. Keeping values as strings on disk makes serialization
trivial; we only convert to `int` when we actually need to compare numerically
(that starts in Stage 2).

```
Table
 ├── name    : "users"
 ├── schema  : [ {id, INT}, {name, TEXT}, {age, INT} ]
 └── rows    : [ ["1","Alice","30"], ["2","Bob","25"] ]
```

## On-disk format

Each table is one text file: `data/<name>.tbl`.

```
id:INT,name:TEXT,age:INT      <- line 0: the schema header
1<US>Alice<US>30              <- one row per line
2<US>Bob<US>25
```

Fields inside a row are separated by the ASCII **unit separator** byte
(`0x1F`, shown as `^_` by `cat -v`). Why not a comma? Because user text often
contains commas — using a control byte that never appears in normal input means
a value like `"Doe, John"` can't corrupt the row boundaries. This is the same
reasoning real formats use when they pick delimiters.

## Type checking

`Table::insert()` enforces two rules before a row is accepted:

1. **Arity** — the number of values must equal the number of columns.
2. **Type** — every column declared `INT` must actually hold an integer
   (`isInteger()` allows an optional leading sign, then digits only).

If either fails it throws, the REPL catches it, and nothing is written. That is
why `INSERT ... VALUES (three, Carol, 40)` was rejected in the test — `three`
isn't a valid INT for `id`. This is a miniature version of what a production DB
does at write time to protect data integrity.

## The REPL (temporary)

Stage 1 hand-parses just enough to exercise storage:

```
CREATE TABLE users (id INT, name TEXT, age INT)
INSERT INTO users VALUES (1, Alice, 30)
SELECT * FROM users
.tables      -- list tables on disk
.exit        -- quit
```

This crude parser is deliberately throwaway. In **Stage 5** it is replaced by a
real tokenizer + AST parser. Doing storage first means we can validate the
engine before worrying about SQL grammar.

## Build & run

```bash
make          # compiles ./minisql
make run      # launches the REPL
make clean    # removes the binary and data/
```

## How to talk about this in an interview

- *"I store each table as a flat file with a schema header and one row per line,
  fields separated by the 0x1F control byte so user data can't break the
  delimiter."*
- *"Rows are kept as strings on disk and parsed to typed values lazily, only
  when a comparison needs it — that keeps the serialization path simple."*
- *"Inserts are validated for arity and column type before anything touches
  disk, so a bad row never persists."*

## What's next (Stage 2)

Add a `WHERE` clause (comparison predicates), plus `UPDATE` and `DELETE`. That
forces the engine to convert stored strings into typed values for comparison —
the first place the `INT`/`TEXT` distinction actually changes behaviour.
