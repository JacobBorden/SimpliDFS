#ifndef MERKLE_TREE_HPP
#define MERKLE_TREE_HPP

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "utilities/chunk_store.hpp"
#include "utilities/blockio.hpp"

class MerkleTree {
public:
  // Compute a directory hash from child name/hash pairs.
  // Entries should be provided as {name, cid}.
  static std::string hashDirectory(
      const std::vector<std::pair<std::string, std::string>> &entries,
      ChunkStore &store);

  // Return vector of CIDs representing the proof path from @p cid to
  // @p rootCid (inclusive). Empty vector if not found.
  static std::vector<std::string> getProofPath(const std::string &rootCid,
                                               const std::string &cid);

  // Verify a proof path previously returned by getProofPath().
  static bool verifyProof(const std::string &rootCid,
                          const std::string &cid,
                          const std::vector<std::string> &proof);

private:
  static DigestResult
  computeDigest(const std::vector<std::pair<std::string, std::string>> &sorted);
  static bool buildProof(const std::string &current,
                         const std::string &target,
                         std::vector<std::string> &path);

  static std::unordered_map<
      std::string,
      std::vector<std::pair<std::string, std::string>>> nodeMap_;
};

#endif // MERKLE_TREE_HPP
