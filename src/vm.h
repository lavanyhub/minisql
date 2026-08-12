#ifndef MINISQL_VM_H
#define MINISQL_VM_H

// ---------------------------------------------------------------------------
// vm.h  —  the bytecode Virtual Machine (the "execution engine")
//
// The Python front-end compiles SQL into a list of simple bytecode
// instructions (tab-separated text, one op per line). This VM executes them.
// It knows nothing about SQL grammar — that separation is exactly how real
// databases split a *planner/compiler* from an *executor*.
//
// Execution model: a pipeline over a single "current result set" (`cur_`).
//   SCAN   loads a table into cur_
//   FILTER / SEEK narrow cur_
//   JOIN   combines cur_ with another table
//   GROUP  collapses cur_ into aggregates
//   PROJECT/OUTPUT shape and print cur_
//   INSERT / UPDATE / DELETE / CREATE mutate tables on disk
// ---------------------------------------------------------------------------
#include "table.h"
#include <string>
#include <vector>

namespace minisql {

// A result set is a schema plus rows. rowIds/source let UPDATE and DELETE map a
// filtered row back to its position in the base table on disk.
struct ResultSet {
    Schema schema;
    std::vector<Row> rows;
    std::vector<int> rowIds;   // original index in source table (when meaningful)
    std::string source;        // base table name (when meaningful)
};

class VM {
public:
    explicit VM(std::string dataDir) : dataDir_(std::move(dataDir)) {}

    // Execute a whole bytecode program (newline-separated instructions).
    void run(const std::string& program);

private:
    std::string dataDir_;
    ResultSet cur_;

    // one handler per opcode
    void opCreate(const std::vector<std::string>& a);
    void opInsert(const std::vector<std::string>& a);
    void opScan(const std::vector<std::string>& a);
    void opSeek(const std::vector<std::string>& a);
    void opFilter(const std::vector<std::string>& a);
    void opProject(const std::vector<std::string>& a);
    void opJoin(const std::vector<std::string>& a);
    void opGroup(const std::vector<std::string>& a);
    void opHaving(const std::vector<std::string>& a);
    void opOrder(const std::vector<std::string>& a);
    void opDelete(const std::vector<std::string>& a);
    void opUpdate(const std::vector<std::string>& a);
    void opOutput();

    void printResult() const;
};

} // namespace minisql

#endif // MINISQL_VM_H
