# How MiniSQL Works — Explained Simply

This document explains the whole project in plain language, step by step.
No jargon without an explanation first.

## What is this project, in one sentence?

You type a command like `SELECT * FROM users`, and this program understands
it, finds the data, and shows it to you — just like a tiny version of a real
database (MySQL, PostgreSQL) but built from scratch so you understand every
piece.

## The big picture — what happens when you type a query

Imagine you type:

```
SELECT name FROM users WHERE age > 30;
```

Here's the journey that one line takes, step by step:

```
STEP 1: You type SQL text
        "SELECT name FROM users WHERE age > 30"
              |
              v
STEP 2: TOKENIZER breaks it into pieces (tokens)
        ["SELECT", "name", "FROM", "users", "WHERE", "age", ">", "30"]
              |
              v
STEP 3: PARSER understands the grammar and builds a tree structure (AST)
        "this is a SELECT, column=name, table=users, condition=(age > 30)"
              |
              v
STEP 4: COMPILER turns that tree into simple step-by-step instructions
        (called bytecode)
        SEEK    users   age   >   30
        PROJECT name
        OUTPUT
              |
              v
STEP 5: The C++ ENGINE reads those instructions and actually runs them
        against the data files on disk
              |
              v
STEP 6: You see the answer printed as a table
```

Two programs work together here:
- **`minisql.py`** (Python) = the "translator." It only understands SQL
  grammar. It never touches the actual data.
- **`minisql`/`minisql.exe`** (C++) = the "worker." It never reads SQL. It
  only understands simple instructions like "get me table X" or "keep rows
  where column Y is bigger than Z."

This separation is exactly how real databases are built — one part
understands language, the other part does the actual work.

## Step 1: How data is stored

Every table is just a text file on your disk, inside the `data/` folder.

```
data/users.tbl
```

Opening it in a text editor would show something like:

```
id:INT,name:TEXT,age:INT      <- first line: column names + types
1|Alice|30                    <- one row per line (real file uses an invisible separator)
2|Bob|25
```

That's it. No fancy database software — just organized text files, read and
written by our own code.

## Step 2: How the tokenizer works

The tokenizer's job is to chop the SQL sentence into meaningful pieces, the
same way you'd split an English sentence into words.

```
"SELECT name FROM users"
        becomes
["SELECT", "name", "FROM", "users"]
```

It knows the difference between a word, a number, a symbol like `>`, and text
in quotes.

## Step 3: How the parser works

The parser reads the token list and figures out **what kind of command** this
is (SELECT? INSERT? DELETE?) and organizes the pieces into a clear structure —
like sorting ingredients into "these go in the pot" and "these go on top."

For `SELECT name FROM users WHERE age > 30`, it produces something like:

```
Select:
  columns: [name]
  table: users
  where: (age, >, 30)
```

## Step 4: How the compiler works

The compiler turns that structure into a short list of very simple
instructions — like a recipe with numbered steps a robot could follow with no
thinking required.

```
SEEK    users   age   >   30      <- find matching rows fast, using an index
PROJECT name                      <- only keep the "name" column
OUTPUT                            <- print the result
```

One smart decision happens here: if you're filtering a number column with
`>`, `<`, `=`, etc., the compiler picks `SEEK` (fast, uses an index) instead of
`SCAN` (slow, checks every row one by one). This decision-making is called
**query planning** — it's the compiler choosing the fastest way to get your
answer.

## Step 5: How the C++ engine executes it

The engine reads those instructions one at a time and does exactly what they
say. It doesn't know anything about SQL — it just knows how to:
- load a table from disk
- keep only rows that match a condition
- combine two tables together (JOIN)
- count/sum/average groups of rows (GROUP BY)
- sort rows
- write changes back to disk

## What's a "B+ Tree" and why does it matter?

Imagine you have a phone book with 1 million names, and you want to find
"Smith." Two ways to search:

- **Without an index:** check every single name from the start until you find
  it. If it's near the end, that's 1 million checks. This is called a
  **linear scan** — it's what `SCAN` does.
- **With an index (like a phone book's alphabetical tabs):** jump straight to
  the "S" section, narrow down fast. This might take only ~20 checks instead
  of 1 million. This is what a **B+ Tree** does — it's a smart, organized
  structure that lets you jump straight to the answer instead of checking
  everything.

I benchmarked this in the project: over 200,000 rows, the B+ tree was
**~20x faster** than checking every row one at a time.

Why not just use the fastest option always (a hash map, like a dictionary)? A
hash map is even faster for "find this exact value," but it **cannot answer
range questions** like "find everyone between age 20 and 30" — it can only
find exact matches. The B+ tree can do both exact matches AND ranges, which is
why real databases use it.

## What did I just improve for "bigger data"?

You asked how to make it handle bigger problems better. I fixed two spots
that would have gotten slow as the data grows:

### Fix 1: JOIN (combining two tables)

**Before:** to combine `users` with `depts`, the program compared every user
against every department, one by one. With 10,000 users and 10,000
departments, that's 100,000,000 comparisons.

**After:** the program builds a quick lookup table (like an index card box)
of the second table first, then looks each user up directly. Same 10,000 x
10,000 case now takes about 20,000 steps instead of 100 million.

**Analogy:** old way = checking every single page of a phone book for every
name you're looking for. New way = building the phone book's index first,
then looking each name up directly.

### Fix 2: GROUP BY (grouping rows, like "count users per department")

**Before:** for every row, the program scanned through the list of groups
found so far to check "have I seen this group before?" — slow if there are
many different groups.

**After:** it uses the same "quick lookup table" idea, so checking "have I
seen this group before?" is instant instead of a re-scan.

Both fixes keep the exact same results — I re-ran the full demo after making
them and got identical output — they're just faster under the hood, which
matters once the data gets big.

## Two more upgrades: AND/OR, and real tests

### 1. WHERE now supports AND / OR

Before, you could only filter on one condition at a time, like `WHERE age > 30`.
Now you can combine conditions:

```sql
SELECT name FROM users WHERE age > 30 AND dept = 10;
SELECT name FROM users WHERE name = 'Bob' OR name = 'Dave';
SELECT name FROM users WHERE age > 30 AND dept = 10 OR name = 'Bob';
```

That last one follows the same rule as real SQL and normal math: **AND binds
tighter than OR**, so it's read as `(age > 30 AND dept = 10) OR name = 'Bob'`,
not left-to-right. The parser builds this correctly by parsing "groups of AND"
first, then joining those groups with OR — this mirrors how a calculator
respects `×` before `+`.

### 2. A real automated test suite (not just "did it crash")

Before, the only proof the project worked was a demo script that printed
output for you to eyeball. That catches "it crashed" but not "it gave the
wrong answer."

Now there are two test files that actually **assert** the correct result and
fail loudly if it's wrong:

- `tests/bptree_test.cpp` — tests the B+ tree in total isolation: does
  `find()` return the right row after 500 scattered inserts and many internal
  splits? Does a range query return exactly the rows in range, no more, no
  less?
- `tests/test_engine.py` — tests the whole engine end to end: create a known
  dataset, run a query, and check the exact rows that came back are the ones
  that should have come back.

**These tests actually caught a real bug while I was building them.**
`SELECT name FROM users ORDER BY age DESC` was silently not sorting, because
the code was deleting the `age` column (to only keep `name`) *before* trying
to sort by it — so the sort had nothing to sort with, and gave up silently
with no error. A demo script wouldn't have caught this unless you happened to
manually check the exact order by eye. A test with a real assertion caught it
immediately. This is the entire reason automated tests matter more than "I
ran it once and it looked fine."

## What else could be improved (future ideas)

Being upfront about the current limits, in case you want to keep building:

1. **Whole table loaded into memory.** Right now, reading a table pulls the
   whole file into RAM. Fine for thousands of rows; would need "streaming" (read
   a bit at a time) for millions of rows.
2. **Index isn't saved.** The B+ tree index is rebuilt fresh every time you run
   a query. A production database saves the index to disk so it doesn't have to
   rebuild it every time.
3. **No concurrent users.** Only one person/program can safely use it at a time
   — there's no system yet to handle multiple people reading/writing at once
   safely.
4. **No transactions.** If the program crashes mid-write, there's no guarantee
   of a clean state (real databases use transaction logs to prevent this).

Mentioning these honestly in an interview is actually a good sign — it shows
you understand the trade-offs you made, not just that you copied code.

## How does someone who won't run the code know it actually works?

This is a real problem: a recruiter or HR person looking at your GitHub isn't
going to download your project and type build commands. So how do they know
it's not just code that looks nice but is actually broken?

**The answer: a build badge.** I set up something called **GitHub Actions** —
think of it as a robot that lives on GitHub. Every single time you push new
code, that robot automatically:

1. Downloads your code fresh, exactly like a stranger would
2. Tries to compile it
3. Runs your demo and checks nothing crashes
4. Puts a green "passing" checkmark (or red "failing" X) right at the top of
   your README

That checkmark is visible the second anyone opens your repo — no setup, no
commands, nothing to run. It's proof, not a claim. This is the same thing
real companies use before merging code, so it also shows you understand a
professional software workflow (this is usually called **CI**, short for
"Continuous Integration").

## Quick recap — the whole system in 5 lines

1. You type SQL → the Python front-end reads and understands it.
2. It compiles your request into a short list of simple steps (bytecode).
3. The C++ engine follows those steps exactly.
4. It stores data as plain text files, and uses a B+ Tree to search fast when
   possible.
5. Fetching, joining, grouping, sorting — all handled by C++ code you can read
   line by line, no hidden magic, no external libraries.
