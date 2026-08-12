# Setup & Requirements

This project has **no third-party libraries** — no `pip install`, no package
manager. You only need a C++ compiler and Python. Here's how to get them on
each platform.

## What you need

| Tool | Why | Check it's installed |
|------|-----|----------------------|
| g++ (C++17) | compiles the engine | `g++ --version` |
| Python 3.8+ | runs the SQL front-end | `python3 --version` |
| make | one-command build (optional) | `make --version` |

## Windows (you're here)

`g++` and `make` aren't native to Windows. Two options:

### Option A — WSL (recommended)

WSL gives you a real Linux toolchain inside Windows.

```powershell
wsl --install          # in an admin PowerShell, then reboot
```

Then open "Ubuntu" from the Start menu and run:

```bash
sudo apt update
sudo apt install -y g++ make python3
cd /mnt/c/Users/lavan/Desktop/automate/minisql
make
python3 minisql.py examples/demo.sql
```

### Option B — MinGW-w64 (native, no Linux)

1. Install MSYS2 from https://www.msys2.org
2. In the MSYS2 terminal: `pacman -S mingw-w64-ucrt-x86_64-gcc make`
3. Add `C:\msys64\ucrt64\bin` to your PATH.
4. Install Python from https://python.org (tick "Add to PATH").
5. In a normal terminal:
   ```
   cd C:\Users\lavan\Desktop\automate\minisql
   mingw32-make          # or: g++ -std=c++17 -O2 src/*.cpp -o minisql
   python minisql.py examples\demo.sql
   ```
   (On native Windows use `python` instead of `python3`.)

## macOS

```bash
xcode-select --install     # gives you clang++ and make
brew install python3       # if not already present
make
python3 minisql.py
```

## Linux

```bash
sudo apt install -y g++ make python3     # Debian/Ubuntu
make
python3 minisql.py
```

## Verify it works

```bash
python3 minisql.py examples/demo.sql
```

You should see tables printed as ASCII grids, a line like
`[index] B+ tree seek on age > 30 -> 2 row(s)`, and correct JOIN/GROUP BY output.

## requirements.txt

There is a `requirements.txt` for convention, but it is intentionally empty of
dependencies — the front-end uses only the Python standard library.
