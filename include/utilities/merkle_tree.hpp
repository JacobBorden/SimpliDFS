#ifndef MERKLE_TREE_HPP
#define MERKLE_TREE_HPP

#include "utilities/chunk_store.hpp"
#include <string>
#include <utility>
#include <vector>

struct MerkleNode {
  bool isDir{false};
  std::string cid;      // Leaf CID or computed directory CID
  std::string entryCid; // Hash of name+cid when part of a directory
  std::vector<std::pair<std::string, MerkleNode>> children;
};

class MerkleTree {
public:
  // Compute a directory hash from child name/hash pairs.
  // Entries should be provided as {name, cid}.
  static std::string
  hashDirectory(const std::vector<std::pair<std::string, std::string>> &entries,
                ChunkStore &store,
                std::vector<std::string> *nodeCids = nullptr);

  // Recursively compute hashes for a directory tree and store intermediate
  // nodes. Returns the root CID.
  static std::string buildTree(MerkleNode &root, ChunkStore &store);

  // Return Merkle proof path for the given CID. Path is leaf-to-root.
  static std::vector<std::string> getProofPath(const MerkleNode &root,
                                               const std::string &cid);

  // Verify a proof path using stored chunks.
  static bool verifyProof(const std::string &cid,
                          const std::vector<std::string> &proof,
                          const ChunkStore &store);
};

#endif // MERKLE_TREE_HPP
