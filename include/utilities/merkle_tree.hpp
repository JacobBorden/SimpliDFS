#ifndef MERKLE_TREE_HPP
#define MERKLE_TREE_HPP

#include "utilities/chunk_store.hpp"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class MerkleTree {
public:
  // Compute a directory hash from child name/hash pairs.
  // Entries should be provided as {name, cid}.
  static std::string
  hashDirectory(const std::vector<std::pair<std::string, std::string>> &entries,
                ChunkStore &store);

  struct DirEntry {
    std::string name;
    bool isFile{false};
    std::vector<std::byte> data;    // for files
    std::vector<DirEntry> children; // for directories
  };

  static std::string
  buildTree(const DirEntry &root, ChunkStore &store,
            std::unordered_map<std::string, std::string> &parents);

  static std::vector<std::string>
  proofPath(const std::string &cid,
            const std::unordered_map<std::string, std::string> &parents);

  static bool verifyProof(const std::vector<std::string> &path,
                          const ChunkStore &store);

private:
  static bool
  parseDirectory(const std::vector<std::byte> &raw,
                 std::vector<std::pair<std::string, std::string>> &entries);
};

#endif // MERKLE_TREE_HPP
