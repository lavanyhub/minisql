#ifndef MINISQL_BPTREE_H
#define MINISQL_BPTREE_H

// ---------------------------------------------------------------------------
// bptree.h  —  a B+ Tree mapping an integer key -> list of row ids
//
// Why a B+ tree and not a plain BST or a hash map?
//   - A B+ tree keeps keys SORTED and keeps all data in the LEAVES, which are
//     linked left-to-right. That makes both point lookups (O(log n)) AND range
//     scans (WHERE age BETWEEN 20 AND 30) efficient — a hash index can do point
//     lookups but NOT ranges.
//   - High fan-out (ORDER children per node) means the tree stays very shallow,
//     so few nodes are touched per lookup — which on real systems means few
//     disk pages read.
//
// This is an in-memory B+ tree built over a column's values. Keys are the
// integer values in that column; the payload is the list of row indices that
// hold that key (a list, because values need not be unique).
// ---------------------------------------------------------------------------
#include <vector>
#include <algorithm>
#include <memory>

namespace minisql {

class BPlusTree {
public:
    // ORDER = max children per internal node. Small here so splits are easy to
    // observe/test; production trees use hundreds to match a disk page.
    static const int ORDER = 4;

    struct Node {
        bool leaf;
        std::vector<long long> keys;                 // sorted keys
        std::vector<std::vector<int>> vals;          // leaf: row ids per key
        std::vector<std::shared_ptr<Node>> children; // internal: child pointers
        std::shared_ptr<Node> next;                  // leaf: link to next leaf
        explicit Node(bool isLeaf) : leaf(isLeaf) {}
    };

    BPlusTree() : root_(std::make_shared<Node>(true)) {}

    // Insert (key -> rowId). Duplicate keys accumulate row ids.
    void insert(long long key, int rowId) {
        auto r = root_;
        if (r->keys.size() == static_cast<size_t>(ORDER - 1) && isFull(r)) {
            // grow the tree one level: new root over a split of the old root
            auto s = std::make_shared<Node>(false);
            s->children.push_back(r);
            splitChild(s, 0);
            root_ = s;
        }
        insertNonFull(root_, key, rowId);
    }

    // Point lookup: all row ids with exactly this key.
    std::vector<int> find(long long key) const {
        auto n = root_;
        while (!n->leaf) {
            int i = 0;
            while (i < static_cast<int>(n->keys.size()) && key >= n->keys[i]) ++i;
            n = n->children[i];
        }
        for (size_t i = 0; i < n->keys.size(); ++i)
            if (n->keys[i] == key) return n->vals[i];
        return {};
    }

    // Range scan: every row id whose key is in [lo, hi], using the linked leaves.
    std::vector<int> range(long long lo, long long hi) const {
        std::vector<int> out;
        auto n = root_;
        while (!n->leaf) {              // descend to the leaf holding lo
            int i = 0;
            while (i < static_cast<int>(n->keys.size()) && lo >= n->keys[i]) ++i;
            n = n->children[i];
        }
        while (n) {                     // walk leaves left-to-right
            for (size_t i = 0; i < n->keys.size(); ++i) {
                if (n->keys[i] < lo) continue;
                if (n->keys[i] > hi) return out;
                out.insert(out.end(), n->vals[i].begin(), n->vals[i].end());
            }
            n = n->next;
        }
        return out;
    }

private:
    std::shared_ptr<Node> root_;

    static bool isFull(const std::shared_ptr<Node>& n) {
        return n->keys.size() >= static_cast<size_t>(ORDER - 1);
    }

    // Split child i of parent, which is assumed full.
    void splitChild(std::shared_ptr<Node> parent, int i) {
        auto child = parent->children[i];
        auto right = std::make_shared<Node>(child->leaf);
        int mid = (ORDER - 1) / 2;

        if (child->leaf) {
            // move the upper half of keys/vals to the new right leaf
            right->keys.assign(child->keys.begin() + mid, child->keys.end());
            right->vals.assign(child->vals.begin() + mid, child->vals.end());
            child->keys.resize(mid);
            child->vals.resize(mid);
            // maintain the leaf linked-list
            right->next = child->next;
            child->next = right;
            // copy up the first key of the right leaf as a separator
            parent->keys.insert(parent->keys.begin() + i, right->keys.front());
            parent->children.insert(parent->children.begin() + i + 1, right);
        } else {
            // internal split: middle key moves UP into the parent
            long long up = child->keys[mid];
            right->keys.assign(child->keys.begin() + mid + 1, child->keys.end());
            right->children.assign(child->children.begin() + mid + 1, child->children.end());
            child->keys.resize(mid);
            child->children.resize(mid + 1);
            parent->keys.insert(parent->keys.begin() + i, up);
            parent->children.insert(parent->children.begin() + i + 1, right);
        }
    }

    void insertNonFull(std::shared_ptr<Node> n, long long key, int rowId) {
        if (n->leaf) {
            // find position; if key already present, append the row id
            int i = 0;
            while (i < static_cast<int>(n->keys.size()) && n->keys[i] < key) ++i;
            if (i < static_cast<int>(n->keys.size()) && n->keys[i] == key) {
                n->vals[i].push_back(rowId);
            } else {
                n->keys.insert(n->keys.begin() + i, key);
                n->vals.insert(n->vals.begin() + i, std::vector<int>{rowId});
            }
        } else {
            int i = 0;
            while (i < static_cast<int>(n->keys.size()) && key >= n->keys[i]) ++i;
            if (isFull(n->children[i])) {
                splitChild(n, i);
                if (key >= n->keys[i]) ++i;   // key may now belong to the new sibling
            }
            insertNonFull(n->children[i], key, rowId);
        }
    }
};

} // namespace minisql

#endif // MINISQL_BPTREE_H
