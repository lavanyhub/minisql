// ---------------------------------------------------------------------------
// tests/bptree_test.cpp — a real unit test for the B+ tree data structure.
//
// This tests the B+ tree directly and in isolation, with no SQL, no files,
// no engine involved — just the data structure itself. That's what a "unit"
// test means: testing one unit of code by itself.
//
// Build & run:
//   g++ -std=c++17 -O2 tests/bptree_test.cpp -I src -o tests/bptree_test
//   ./tests/bptree_test
//
// Exits with code 0 if everything passes, 1 if anything fails (so it plugs
// straight into CI — a nonzero exit code fails the build).
// ---------------------------------------------------------------------------
#include "../src/bptree.h"
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstdlib>

using minisql::BPlusTree;

static int failures = 0;

// A tiny hand-rolled assertion helper: prints PASS/FAIL per check instead of
// aborting on the first failure, so one run shows everything that's wrong.
static void check(bool condition, const std::string& description) {
    if (condition) {
        std::cout << "  [PASS] " << description << "\n";
    } else {
        std::cout << "  [FAIL] " << description << "\n";
        failures++;
    }
}

static bool sameSet(std::vector<int> a, std::vector<int> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

int main() {
    std::cout << "B+ Tree unit tests\n-------------------\n";

    // --- Test 1: point lookup on a small, known set of keys ---
    {
        std::cout << "Test 1: basic point lookup\n";
        BPlusTree t;
        t.insert(10, 0);
        t.insert(20, 1);
        t.insert(30, 2);
        check(t.find(20) == std::vector<int>{1}, "find(20) returns row id 1");
        check(t.find(999).empty(), "find on a missing key returns nothing");
    }

    // --- Test 2: duplicate keys accumulate multiple row ids ---
    {
        std::cout << "Test 2: duplicate keys\n";
        BPlusTree t;
        t.insert(5, 0);
        t.insert(5, 1);
        t.insert(5, 2);
        check(sameSet(t.find(5), {0, 1, 2}), "find(5) returns all three row ids for a duplicate key");
    }

    // --- Test 3: range query returns everything in [lo, hi], nothing outside ---
    {
        std::cout << "Test 3: range scan\n";
        BPlusTree t;
        for (int i = 0; i < 20; ++i) t.insert(i, i);       // keys 0..19, rowId == key
        auto r = t.range(5, 10);
        check(sameSet(r, {5, 6, 7, 8, 9, 10}), "range(5,10) returns exactly keys 5..10");
        check(t.range(100, 200).empty(), "range outside all keys returns nothing");
    }

    // --- Test 4: correctness survives node splits (insert enough keys to force splits) ---
    {
        std::cout << "Test 4: correctness after many splits\n";
        BPlusTree t;
        const int N = 500;
        std::vector<int> keys;
        for (int i = 0; i < N; ++i) {
            int k = (i * 37) % 997;    // scattered, not sorted, to also exercise mid-tree splits
            keys.push_back(k);
            t.insert(k, i);
        }
        bool allFound = true;
        for (int i = 0; i < N; ++i) {
            auto hits = t.find(keys[i]);
            if (std::find(hits.begin(), hits.end(), i) == hits.end()) { allFound = false; break; }
        }
        check(allFound, "every one of 500 inserted keys is still findable after many node splits");

        auto full = t.range(-1000000, 1000000);
        check(full.size() == static_cast<size_t>(N),
              "a full range scan returns exactly as many row ids as were inserted (" +
              std::to_string(full.size()) + " == " + std::to_string(N) + ")");
    }

    std::cout << "-------------------\n";
    if (failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    } else {
        std::cout << failures << " test(s) FAILED.\n";
        return 1;
    }
}
