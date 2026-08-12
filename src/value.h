#ifndef MINISQL_VALUE_H
#define MINISQL_VALUE_H

// ---------------------------------------------------------------------------
// value.h  —  typed comparison helpers
//
// Rows are stored as strings (see table.h). The moment we need to evaluate a
// predicate like `age > 25` we must compare *with the column's type in mind*:
//   - INT  columns compare numerically   ("10" > "9"   -> true)
//   - TEXT columns compare lexicographically ("apple" < "banana")
//
// This header centralises that "lazy conversion" so every operator uses the
// same rules.
// ---------------------------------------------------------------------------
#include <string>
#include <cstdlib>
#include "table.h"

namespace minisql {

// Three-way compare of two stored strings under a given column type.
// Returns <0 if a<b, 0 if equal, >0 if a>b.
inline long long compareTyped(const std::string& a, const std::string& b, ColType t) {
    if (t == ColType::INT) {
        long long ai = std::atoll(a.c_str());
        long long bi = std::atoll(b.c_str());
        return (ai > bi) - (ai < bi);
    }
    return a.compare(b);   // TEXT: lexicographic
}

// Evaluate a comparison operator given the sign of compareTyped().
// op is one of: =  !=  <  <=  >  >=
inline bool applyOp(const std::string& op, long long cmp) {
    if (op == "=")  return cmp == 0;
    if (op == "!=") return cmp != 0;
    if (op == "<")  return cmp <  0;
    if (op == "<=") return cmp <= 0;
    if (op == ">")  return cmp >  0;
    if (op == ">=") return cmp >= 0;
    return false;
}

} // namespace minisql

#endif // MINISQL_VALUE_H
