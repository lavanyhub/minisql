# MiniSQL build file
# `make`        -> builds the ./minisql binary
# `make run`    -> builds then launches the REPL
# `make clean`  -> removes build artifacts and the data/ directory

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
SRC      := src/main.cpp src/table.cpp src/vm.cpp
HDR      := src/table.h src/value.h src/bptree.h src/vm.h
BIN      := minisql

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

# `make run` launches the SQL REPL (Python front-end driving the C++ engine)
run: $(BIN)
	python3 minisql.py

clean:
	rm -f $(BIN)
	rm -rf data

.PHONY: run clean
