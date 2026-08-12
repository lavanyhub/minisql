// ---------------------------------------------------------------------------
// main.cpp  —  the backend entry point
//
// The C++ program is now a pure bytecode executor. It reads a bytecode program
// (one instruction per line, tab-separated) from stdin and runs it against the
// on-disk tables in the data directory.
//
//     ./minisql            # reads bytecode from stdin, uses ./data
//     ./minisql mydata     # use a custom data directory
//
// The human-facing SQL REPL lives in the Python front-end (minisql.py), which
// compiles SQL to this bytecode and pipes it here. Keeping the executor
// separate from the parser mirrors how real databases split planning from
// execution.
// ---------------------------------------------------------------------------
#include "vm.h"
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    std::string dataDir = (argc > 1) ? argv[1] : "data";

    std::stringstream buf;
    buf << std::cin.rdbuf();          // slurp the whole bytecode program

    minisql::VM vm(dataDir);
    vm.run(buf.str());
    return 0;
}
