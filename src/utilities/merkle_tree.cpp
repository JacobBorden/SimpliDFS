#include "utilities/merkle_tree.hpp"
#include "utilities/blockio.hpp"
#include <algorithm>
#include <unordered_map>

std::unordered_map<std::string,
                   std::vector<std::pair<std::string, std::string>>>
    MerkleTree::nodeMap_;

DigestResult MerkleTree::computeDigest(
    const std::vector<std::pair<std::string, std::string>> &sorted) {
  BlockIO bio;
  for (const auto &e : sorted) {
    std::vector<std::byte> name_bytes;
    for (char c : e.first)
      name_bytes.push_back(std::byte(c));
    bio.ingest(name_bytes.data(), name_bytes.size());

    std::vector<std::byte> hash_bytes;
    for (char c : e.second)
      hash_bytes.push_back(std::byte(c));
    bio.ingest(hash_bytes.data(), hash_bytes.size());
  }
  return bio.finalize_hashed();
}

std::string MerkleTree::hashDirectory(
    const std::vector<std::pair<std::string, std::string>> &entries,
    ChunkStore &store) {
  std::vector<std::pair<std::string, std::string>> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  DigestResult dr = computeDigest(sorted);
  store.addChunk(dr.raw); // store directory representation itself
  nodeMap_[dr.cid] = sorted;
  return dr.cid;
}

bool MerkleTree::buildProof(const std::string &current,
                            const std::string &target,
                            std::vector<std::string> &path) {
  if (current == target) {
    path.push_back(current);
    return true;
  }
  auto it = nodeMap_.find(current);
  if (it == nodeMap_.end())
    return false;
  for (const auto &child : it->second) {
    if (buildProof(child.second, target, path)) {
      path.push_back(current);
      return true;
    }
  }
  return false;
}

std::vector<std::string> MerkleTree::getProofPath(const std::string &rootCid,
                                                  const std::string &cid) {
  std::vector<std::string> path;
  buildProof(rootCid, cid, path);
  return path;
}

bool MerkleTree::verifyProof(const std::string &rootCid, const std::string &cid,
                             const std::vector<std::string> &proof) {
  if (proof.empty() || proof.front() != cid || proof.back() != rootCid)
    return false;

  for (size_t i = 1; i < proof.size(); ++i) {
    const std::string &child = proof[i - 1];
    const std::string &parent = proof[i];
    auto it = nodeMap_.find(parent);
    if (it == nodeMap_.end())
      return false;
    DigestResult dr = computeDigest(it->second);
    if (dr.cid != parent)
      return false;
    bool found = false;
    for (const auto &p : it->second) {
      if (p.second == child) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}
