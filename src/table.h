#ifndef MINISQL_TABLE_H
#define MINISQL_TABLE_H

// ---------------------------------------------------------------------------
// table.h  —  Schema, Row and Table (the storage layer)
//
// This is the heart of Stage 1. A real DBMS stores rows on disk in "pages",
// but to keep the internals readable we store each table as ONE flat text
// file on disk:
//
//     data/<tablename>.tbl
//
// File layout:
//     line 0 : the schema     ->  id:INT,name:TEXT,age:INT
//     line 1 : first row       ->  1\x1fAlice\x1f30
//     line 2 : second row      ->  2\x1fBob\x1f25
//     ...
//
// We use the ASCII "unit separator" byte (0x1F) between fields instead of a
// comma so that user text containing commas/tabs never corrupts a row.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>
#include <cstdint>

namespace minisql {

// A column has a name and a type. Stage 1 supports just two types; that is
// enough to demonstrate typed storage, and we widen it later.
enum class ColType { INT, TEXT };

struct Column {
    std::string name;
    ColType     type;
};

// The schema is simply the ordered list of columns for a table.
using Schema = std::vector<Column>;

// A row is stored as a vector of strings (one per column). Keeping everything
// as strings on disk keeps serialization trivial; the engine converts to int
// only when it needs to (e.g. comparisons in later stages).
using Row = std::vector<std::string>;

class Table {
public:
    std::string name;
    Schema      schema;
    std::vector<Row> rows;   // the whole table, held in memory while open

    Table() = default;
    Table(std::string tableName, Schema tableSchema)
        : name(std::move(tableName)), schema(std::move(tableSchema)) {}

    // Return the 0-based position of a column by name, or -1 if not found.
    int columnIndex(const std::string& col) const;

    // Append one row. Throws std::runtime_error if the arity or an INT type
    // does not match the schema — this is our first taste of type checking.
    void insert(const Row& values);

    // Persist the in-memory table to data/<name>.tbl (overwrites the file).
    void saveToDisk(const std::string& dataDir) const;

    // Load a table (schema + all rows) back from disk.
    static Table loadFromDisk(const std::string& dataDir, const std::string& tableName);

    // Helpers to turn a schema into/out of the header line on disk.
    static std::string  schemaToString(const Schema& s);
    static Schema       schemaFromString(const std::string& line);
    static const char*  typeName(ColType t);
};

} // namespace minisql

#endif // MINISQL_TABLE_H
