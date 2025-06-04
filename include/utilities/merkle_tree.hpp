#ifndef MERKLE_TREE_HPP
#define MERKLE_TREE_HPP

#include <string>
#include <vector>
#include <utility>
#include "utilities/chunk_store.hpp"

class MerkleTree {
public:
    // Compute a directory hash from child name/hash pairs.
    // Entries should be provided as {name, cid}.
    static std::string hashDirectory(const std::vector<std::pair<std::string, std::string>>& entries, ChunkStore& store);
};

#endif // MERKLE_TREE_HPP
