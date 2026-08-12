// ---------------------------------------------------------------------------
// vm.cpp  —  implementation of the bytecode VM
// ---------------------------------------------------------------------------
#include "vm.h"
#include "value.h"
#include "bptree.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <climits>
#include <unordered_map>

namespace minisql {

static const char UNIT  = '\x1f';   // separates list items packed into one arg
static const char COND  = '\x1e';   // separates conditions inside one AND-group
static const char GROUP = '\x1d';   // separates AND-groups (OR'd together)

// generic "split string on one separator byte" used by several packed formats
static std::vector<std::string> splitChar(const std::string& s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, sep)) out.push_back(t);
    return out;
}

// split "a\x1fb\x1fc" -> [a,b,c]
static std::vector<std::string> splitUnit(const std::string& s) {
    return splitChar(s, UNIT);
}

// split "a,b,c" -> [a,b,c] (trimmed)
static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string t;
    while (std::getline(ss, t, ',')) {
        size_t a = t.find_first_not_of(" \t");
        size_t b = t.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(t.substr(a, b - a + 1));
    }
    return out;
}

// -------- CREATE / INSERT (mutating ops) -----------------------------------

void VM::opCreate(const std::vector<std::string>& a) {
    // CREATE <name> <col:type,col:type,...>
    Schema schema;
    for (const auto& part : splitComma(a[1])) {
        auto colon = part.find(':');
        Column c;
        c.name = part.substr(0, colon);
        std::string ty = part.substr(colon + 1);
        c.type = (ty == "INT") ? ColType::INT : ColType::TEXT;
        schema.push_back(c);
    }
    Table t(a[0], schema);
    t.saveToDisk(dataDir_);
    std::cout << "Table '" << a[0] << "' created.\n";
}

void VM::opInsert(const std::vector<std::string>& a) {
    // INSERT <name> <v1\x1fv2...>
    Table t = Table::loadFromDisk(dataDir_, a[0]);
    t.insert(splitUnit(a[1]));
    t.saveToDisk(dataDir_);
    std::cout << "1 row inserted.\n";
}

// -------- SCAN / SEEK (produce a result set) -------------------------------

void VM::opScan(const std::vector<std::string>& a) {
    Table t = Table::loadFromDisk(dataDir_, a[0]);
    cur_.schema = t.schema;
    cur_.rows   = t.rows;
    cur_.source = a[0];
    cur_.rowIds.clear();
    for (int i = 0; i < static_cast<int>(t.rows.size()); ++i) cur_.rowIds.push_back(i);
}

void VM::opSeek(const std::vector<std::string>& a) {
    // SEEK <name> <col> <op> <value>   — build a B+ index on <col> then probe it
    Table t = Table::loadFromDisk(dataDir_, a[0]);
    int ci = t.columnIndex(a[1]);
    if (ci < 0) throw std::runtime_error("Unknown column in SEEK: " + a[1]);
    if (t.schema[ci].type != ColType::INT)
        throw std::runtime_error("Index seek only supported on INT columns");

    BPlusTree idx;                               // build a dense index on the fly
    for (int i = 0; i < static_cast<int>(t.rows.size()); ++i)
        idx.insert(std::atoll(t.rows[i][ci].c_str()), i);

    long long v = std::atoll(a[3].c_str());
    std::vector<int> ids;
    const std::string& op = a[2];
    if (op == "=")       ids = idx.find(v);
    else if (op == ">")  ids = idx.range(v + 1, LLONG_MAX);
    else if (op == ">=") ids = idx.range(v,     LLONG_MAX);
    else if (op == "<")  ids = idx.range(LLONG_MIN, v - 1);
    else if (op == "<=") ids = idx.range(LLONG_MIN, v);
    else throw std::runtime_error("SEEK op not supported: " + op);

    std::sort(ids.begin(), ids.end());
    cur_.schema = t.schema;
    cur_.source = a[0];
    cur_.rows.clear();
    cur_.rowIds.clear();
    for (int id : ids) { cur_.rows.push_back(t.rows[id]); cur_.rowIds.push_back(id); }
    std::cout << "[index] B+ tree seek on " << a[1] << ' ' << op << ' ' << v
              << " -> " << ids.size() << " row(s)\n";
}

// -------- FILTER / PROJECT --------------------------------------------------

// A single condition: which column, which operator, which literal value.
struct Condition { int colIndex; ColType type; std::string op; std::string value; };

void VM::opFilter(const std::vector<std::string>& a) {
    // FILTER <packed-where-expression>
    //
    // The expression is a list of AND-groups, OR'd together:
    //     group0_cond0 AND group0_cond1   OR   group1_cond0
    // Packed as:  GROUP-separated groups, each group is COND-separated
    // conditions, each condition is UNIT-separated (col, op, value).
    //
    // A row survives if it satisfies EVERY condition in AT LEAST ONE group
    // — that's exactly what "(a AND b) OR c" means.
    std::vector<std::vector<Condition>> groups;
    for (const auto& groupStr : splitChar(a[0], GROUP)) {
        std::vector<Condition> conds;
        for (const auto& condStr : splitChar(groupStr, COND)) {
            auto parts = splitUnit(condStr);
            if (parts.size() != 3) throw std::runtime_error("Malformed WHERE condition");
            int ci = -1;
            for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
                if (cur_.schema[i].name == parts[0]) ci = i;
            if (ci < 0) throw std::runtime_error("Unknown column in WHERE: " + parts[0]);
            conds.push_back({ci, cur_.schema[ci].type, parts[1], parts[2]});
        }
        groups.push_back(conds);
    }

    std::vector<Row> keptRows;
    std::vector<int> keptIds;
    for (size_t r = 0; r < cur_.rows.size(); ++r) {
        const Row& row = cur_.rows[r];
        bool rowMatches = false;
        for (const auto& group : groups) {           // OR across groups
            bool groupMatches = true;
            for (const auto& c : group) {             // AND within a group
                long long cmp = compareTyped(row[c.colIndex], c.value, c.type);
                if (!applyOp(c.op, cmp)) { groupMatches = false; break; }
            }
            if (groupMatches) { rowMatches = true; break; }
        }
        if (rowMatches) {
            keptRows.push_back(row);
            if (r < cur_.rowIds.size()) keptIds.push_back(cur_.rowIds[r]);
        }
    }
    cur_.rows = keptRows;
    cur_.rowIds = keptIds;
}

void VM::opProject(const std::vector<std::string>& a) {
    // PROJECT <col,col,...>  ('*' keeps everything)
    if (a[0] == "*") return;
    std::vector<std::string> cols = splitComma(a[0]);
    std::vector<int> idx;
    Schema ns;
    for (const auto& c : cols) {
        int ci = -1;
        for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
            if (cur_.schema[i].name == c) ci = i;
        if (ci < 0) throw std::runtime_error("Unknown column in SELECT: " + c);
        idx.push_back(ci);
        ns.push_back(cur_.schema[ci]);
    }
    std::vector<Row> nr;
    for (const auto& r : cur_.rows) {
        Row row;
        for (int ci : idx) row.push_back(r[ci]);
        nr.push_back(row);
    }
    cur_.schema = ns;
    cur_.rows = nr;
    cur_.rowIds.clear();   // projection loses row identity
}

// -------- JOIN --------------------------------------------------------------

void VM::opJoin(const std::vector<std::string>& a) {
    // JOIN <name> <leftcol> <rightcol> <type>   type = inner | right
    //
    // HASH JOIN instead of the old nested-loop join.
    //   Nested loop: for every left row, scan every right row  -> O(n * m)
    //   Hash join:   build a hash map of the right table once  -> O(n + m)
    // For 10,000 x 10,000 rows that's the difference between ~100,000,000
    // comparisons and ~20,000 — this is the single biggest win for scaling
    // JOINs to larger tables.
    Table rt = Table::loadFromDisk(dataDir_, a[0]);
    const std::string& type = a[3];

    int lci = -1;
    for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
        if (cur_.schema[i].name == a[1]) lci = i;
    int rci = rt.columnIndex(a[2]);
    if (lci < 0 || rci < 0) throw std::runtime_error("JOIN column not found");

    Schema ns = cur_.schema;
    for (const auto& c : rt.schema) ns.push_back({a[0] + "." + c.name, c.type});

    // Build phase: hash every right-table row by its join-key string, once.
    // (Values are compared as strings here; INT columns were already
    // normalized to plain digit strings by the storage layer, so equal
    // numbers produce equal strings — e.g. no "007" vs "7" mismatch.)
    std::unordered_multimap<std::string, size_t> rightIndex;
    rightIndex.reserve(rt.rows.size() * 2);
    for (size_t j = 0; j < rt.rows.size(); ++j)
        rightIndex.emplace(rt.rows[j][rci], j);

    std::vector<Row> out;
    std::vector<bool> rightMatched(rt.rows.size(), false);

    // Probe phase: for each left row, one hash lookup finds all matches.
    for (const auto& lr : cur_.rows) {
        auto range = rightIndex.equal_range(lr[lci]);
        for (auto it = range.first; it != range.second; ++it) {
            size_t j = it->second;
            Row row = lr;
            for (const auto& v : rt.rows[j]) row.push_back(v);
            out.push_back(row);
            rightMatched[j] = true;
        }
    }
    // RIGHT JOIN: include right rows that never matched, padding left side null
    if (type == "right") {
        for (size_t j = 0; j < rt.rows.size(); ++j) if (!rightMatched[j]) {
            Row row(cur_.schema.size(), "NULL");
            for (const auto& v : rt.rows[j]) row.push_back(v);
            out.push_back(row);
        }
    }
    cur_.schema = ns;
    cur_.rows = out;
    cur_.rowIds.clear();
    cur_.source.clear();
}

// -------- GROUP BY / HAVING -------------------------------------------------

void VM::opGroup(const std::vector<std::string>& a) {
    // GROUP <groupcol|""> <func> <aggcol|"*"> <alias>
    const std::string& gcol = a[0];
    const std::string& func = a[1];   // COUNT SUM AVG MIN MAX
    const std::string& acol = a[2];
    const std::string& alias = a[3];

    int gi = -1, ai = -1;
    if (!gcol.empty())
        for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
            if (cur_.schema[i].name == gcol) gi = i;
    if (acol != "*")
        for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
            if (cur_.schema[i].name == acol) ai = i;

    // Group rows using a hash map instead of a linear "have we seen this key
    // before?" scan. The old version searched the whole `order` list for
    // every row (O(n * g) — g = number of distinct groups); with a hash map
    // each row is placed in O(1) average time (O(n) total). On a table with
    // many distinct group values this is a big difference.
    std::vector<std::string> order;                  // preserves first-seen order
    std::vector<std::vector<double>> nums;            // numeric values per group
    std::vector<long long> counts;
    std::unordered_map<std::string, int> slotOf;      // key -> index into order/nums/counts
    slotOf.reserve(cur_.rows.size());
    auto keyOf = [&](const Row& r) { return gi >= 0 ? r[gi] : std::string("(all)"); };

    for (const auto& r : cur_.rows) {
        std::string k = keyOf(r);
        auto it = slotOf.find(k);
        int slot;
        if (it == slotOf.end()) {
            slot = static_cast<int>(order.size());
            slotOf.emplace(k, slot);
            order.push_back(k);
            nums.emplace_back();
            counts.push_back(0);
        } else {
            slot = it->second;
        }
        counts[slot]++;
        if (ai >= 0) nums[slot].push_back(std::atof(r[ai].c_str()));
    }

    Schema ns;
    if (gi >= 0) ns.push_back(cur_.schema[gi]);
    ns.push_back({alias, ColType::INT});   // aggregate reported as numeric

    std::vector<Row> out;
    for (size_t i = 0; i < order.size(); ++i) {
        double res = 0;
        if (func == "COUNT") res = counts[i];
        else {
            const auto& v = nums[i];
            if (func == "SUM" || func == "AVG") { for (double x : v) res += x; if (func == "AVG" && !v.empty()) res /= v.size(); }
            else if (func == "MIN") { res = v.empty() ? 0 : *std::min_element(v.begin(), v.end()); }
            else if (func == "MAX") { res = v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
        }
        // print integers without trailing .0
        std::ostringstream os;
        if (res == static_cast<long long>(res)) os << static_cast<long long>(res);
        else os << res;
        Row row;
        if (gi >= 0) row.push_back(order[i]);
        row.push_back(os.str());
        out.push_back(row);
    }
    cur_.schema = ns;
    cur_.rows = out;
    cur_.rowIds.clear();
}

void VM::opHaving(const std::vector<std::string>& a) {
    // HAVING <op> <value>  — filters on the aggregate (last column)
    int ci = cur_.schema.size() - 1;
    std::vector<Row> kept;
    for (const auto& r : cur_.rows) {
        long long c = compareTyped(r[ci], a[1], ColType::INT);
        if (applyOp(a[0], c)) kept.push_back(r);
    }
    cur_.rows = kept;
}

void VM::opOrder(const std::vector<std::string>& a) {
    // ORDER <col> <asc|desc>
    int ci = -1;
    for (int i = 0; i < static_cast<int>(cur_.schema.size()); ++i)
        if (cur_.schema[i].name == a[0]) ci = i;
    if (ci < 0) throw std::runtime_error("Unknown column in ORDER BY: " + a[0]);
    ColType ty = cur_.schema[ci].type;
    bool desc = (a.size() > 1 && a[1] == "desc");
    std::stable_sort(cur_.rows.begin(), cur_.rows.end(),
        [&](const Row& x, const Row& y) {
            long long c = compareTyped(x[ci], y[ci], ty);
            return desc ? c > 0 : c < 0;
        });
    cur_.rowIds.clear();
}

// -------- UPDATE / DELETE (write back to disk) -----------------------------

void VM::opDelete(const std::vector<std::string>& a) {
    // DELETE <name>  — removes rows currently in cur_ (identified by rowIds)
    Table t = Table::loadFromDisk(dataDir_, a[0]);
    std::vector<bool> drop(t.rows.size(), false);
    for (int id : cur_.rowIds) if (id >= 0 && id < static_cast<int>(drop.size())) drop[id] = true;
    std::vector<Row> kept;
    for (size_t i = 0; i < t.rows.size(); ++i) if (!drop[i]) kept.push_back(t.rows[i]);
    int removed = static_cast<int>(t.rows.size() - kept.size());
    t.rows = kept;
    t.saveToDisk(dataDir_);
    std::cout << removed << " row(s) deleted.\n";
}

void VM::opUpdate(const std::vector<std::string>& a) {
    // UPDATE <name> <col> <value>  — sets col=value for rows in cur_
    Table t = Table::loadFromDisk(dataDir_, a[0]);
    int ci = t.columnIndex(a[1]);
    if (ci < 0) throw std::runtime_error("Unknown column in UPDATE: " + a[1]);
    int n = 0;
    for (int id : cur_.rowIds)
        if (id >= 0 && id < static_cast<int>(t.rows.size())) { t.rows[id][ci] = a[2]; ++n; }
    t.saveToDisk(dataDir_);
    std::cout << n << " row(s) updated.\n";
}

// -------- output ------------------------------------------------------------

void VM::printResult() const {
    const auto& s = cur_.schema;
    std::vector<size_t> w(s.size());
    for (size_t i = 0; i < s.size(); ++i) w[i] = s[i].name.size();
    for (const auto& r : cur_.rows)
        for (size_t i = 0; i < r.size() && i < w.size(); ++i) w[i] = std::max(w[i], r[i].size());

    auto rule = [&] {
        for (size_t i = 0; i < w.size(); ++i) std::cout << '+' << std::string(w[i] + 2, '-');
        std::cout << "+\n";
    };
    rule();
    for (size_t i = 0; i < s.size(); ++i)
        std::cout << "| " << s[i].name << std::string(w[i] - s[i].name.size(), ' ') << ' ';
    std::cout << "|\n";
    rule();
    for (const auto& r : cur_.rows) {
        for (size_t i = 0; i < w.size(); ++i)
            std::cout << "| " << r[i] << std::string(w[i] - r[i].size(), ' ') << ' ';
        std::cout << "|\n";
    }
    rule();
    std::cout << cur_.rows.size() << " row(s).\n";
}

void VM::opOutput() { printResult(); }

// -------- dispatch ----------------------------------------------------------

void VM::run(const std::string& program) {
    std::stringstream prog(program);
    std::string line;
    while (std::getline(prog, line)) {
        if (line.empty()) continue;
        std::vector<std::string> tok;
        std::stringstream ls(line);
        std::string t;
        while (std::getline(ls, t, '\t')) tok.push_back(t);
        if (tok.empty()) continue;

        std::string op = tok[0];
        std::vector<std::string> args(tok.begin() + 1, tok.end());
        try {
            if      (op == "CREATE")  opCreate(args);
            else if (op == "INSERT")  opInsert(args);
            else if (op == "SCAN")    opScan(args);
            else if (op == "SEEK")    opSeek(args);
            else if (op == "FILTER")  opFilter(args);
            else if (op == "PROJECT") opProject(args);
            else if (op == "JOIN")    opJoin(args);
            else if (op == "GROUP")   opGroup(args);
            else if (op == "HAVING")  opHaving(args);
            else if (op == "ORDER")   opOrder(args);
            else if (op == "DELETE")  opDelete(args);
            else if (op == "UPDATE")  opUpdate(args);
            else if (op == "OUTPUT")  opOutput();
            else std::cerr << "Unknown opcode: " << op << "\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }
}

} // namespace minisql
