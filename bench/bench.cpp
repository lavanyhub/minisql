// ---------------------------------------------------------------------------
// bench.cpp  —  B+ tree index vs linear scan vs hash index
//
// Builds N random keys, then times point lookups three ways:
//   1. linear scan        O(n) per lookup
//   2. B+ tree            O(log n) per lookup, and supports range queries
//   3. std::unordered_map O(1) average, but NO range support
//
// Build:  g++ -std=c++17 -O2 bench/bench.cpp -I src -o bench/bench && ./bench/bench
// ---------------------------------------------------------------------------
#include "../src/bptree.h"
#include <vector>
#include <unordered_map>
#include <random>
#include <chrono>
#include <iostream>

using namespace std;
using namespace std::chrono;
using minisql::BPlusTree;

int main() {
    const int N = 200000;      // rows
    const int Q = 20000;       // lookups to time

    mt19937 rng(42);
    uniform_int_distribution<int> dist(0, N * 10);

    vector<int> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = dist(rng);

    // build the two indexes
    BPlusTree tree;
    unordered_map<int, vector<int>> hash;
    for (int i = 0; i < N; ++i) { tree.insert(keys[i], i); hash[keys[i]].push_back(i); }

    // pick Q random existing keys to look up
    vector<int> probes(Q);
    for (int i = 0; i < Q; ++i) probes[i] = keys[dist(rng) % N];

    auto time_it = [](auto fn) {
        auto t0 = high_resolution_clock::now();
        long long sink = fn();
        auto t1 = high_resolution_clock::now();
        double ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        return make_pair(ms, sink);
    };

    // 1. linear scan — accumulate the found index so the optimizer cannot
    //    prove the result and delete the loop.
    auto [lin_ms, s1] = time_it([&] {
        long long acc = 0;
        for (int p : probes)
            for (int i = 0; i < N; ++i) if (keys[i] == p) { acc += i; break; }
        return acc;
    });

    // 2. B+ tree
    auto [bpt_ms, s2] = time_it([&] {
        long long hits = 0;
        for (int p : probes) hits += tree.find(p).size();
        return hits;
    });

    // 3. hash map
    auto [hash_ms, s3] = time_it([&] {
        long long hits = 0;
        for (int p : probes) { auto it = hash.find(p); if (it != hash.end()) hits += it->second.size(); }
        return hits;
    });
    cout << "N = " << N << " rows, Q = " << Q << " lookups\n";
    cout << "(checksums so the compiler can't delete the loops: "
         << s1 << ' ' << s2 << ' ' << s3 << ")\n";
    cout << "-----------------------------------------------\n";
    cout << "linear scan   : " << lin_ms  << " ms\n";
    cout << "B+ tree       : " << bpt_ms  << " ms   (" << (lin_ms / bpt_ms)  << "x faster than scan)\n";
    cout << "hash map      : " << hash_ms << " ms   (fastest for point, but no range queries)\n";
    cout << "\nTakeaway: B+ tree gives near-hash point speed AND ordered range\n"
            "scans (WHERE age BETWEEN ..), which the hash index cannot do.\n";
    return 0;
}
