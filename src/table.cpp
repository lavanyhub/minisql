// ---------------------------------------------------------------------------
// table.cpp  —  implementation of the storage layer declared in table.h
// ---------------------------------------------------------------------------
#include "table.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace minisql {

// The single byte we use to separate fields inside a stored row.
static const char FIELD_SEP = '\x1f';

const char* Table::typeName(ColType t) {
    return t == ColType::INT ? "INT" : "TEXT";
}

int Table::columnIndex(const std::string& col) const {
    for (int i = 0; i < static_cast<int>(schema.size()); ++i)
        if (schema[i].name == col) return i;
    return -1;
}

// Verify a value is a valid integer (optionally signed). Used for INT columns.
static bool isInteger(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (start == s.size()) return false;          // just a sign, no digits
    for (size_t i = start; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

void Table::insert(const Row& values) {
    // 1) arity check — number of values must match number of columns
    if (values.size() != schema.size())
        throw std::runtime_error("INSERT column count (" +
            std::to_string(values.size()) + ") does not match schema (" +
            std::to_string(schema.size()) + ")");

    // 2) type check — every INT column must actually hold an integer
    for (size_t i = 0; i < schema.size(); ++i)
        if (schema[i].type == ColType::INT && !isInteger(values[i]))
            throw std::runtime_error("Value '" + values[i] +
                "' is not a valid INT for column '" + schema[i].name + "'");

    rows.push_back(values);
}

std::string Table::schemaToString(const Schema& s) {
    std::ostringstream os;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i) os << ',';
        os << s[i].name << ':' << typeName(s[i].type);
    }
    return os.str();
}

Schema Table::schemaFromString(const std::string& line) {
    Schema s;
    std::stringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        auto colon = tok.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("Corrupt schema entry: " + tok);
        Column c;
        c.name = tok.substr(0, colon);
        std::string ty = tok.substr(colon + 1);
        c.type = (ty == "INT") ? ColType::INT : ColType::TEXT;
        s.push_back(c);
    }
    return s;
}

void Table::saveToDisk(const std::string& dataDir) const {
    std::filesystem::create_directories(dataDir);
    std::ofstream out(dataDir + "/" + name + ".tbl", std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot open table file for writing: " + name);

    out << schemaToString(schema) << '\n';          // header line
    for (const auto& r : rows) {
        for (size_t i = 0; i < r.size(); ++i) {
            if (i) out << FIELD_SEP;
            out << r[i];
        }
        out << '\n';
    }
}

Table Table::loadFromDisk(const std::string& dataDir, const std::string& tableName) {
    std::ifstream in(dataDir + "/" + tableName + ".tbl");
    if (!in) throw std::runtime_error("Table does not exist: " + tableName);

    std::string header;
    std::getline(in, header);
    Table t(tableName, schemaFromString(header));

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Row r;
        std::string field;
        std::stringstream ss(line);
        while (std::getline(ss, field, FIELD_SEP)) r.push_back(field);
        // pad in case a trailing empty field was dropped by getline
        while (r.size() < t.schema.size()) r.push_back("");
        t.rows.push_back(r);
    }
    return t;
}

} // namespace minisql
