#include "utilities/merkle_tree.hpp"
#include "utilities/blockio.hpp"
#include <algorithm>
#include <cstring>

static std::string hashNode(const std::string &name, const std::string &cid,
                            ChunkStore &store) {
  BlockIO bio;
  std::vector<std::byte> name_bytes;
  for (char c : name)
    name_bytes.push_back(std::byte(c));
  bio.ingest(name_bytes.data(), name_bytes.size());
  std::vector<std::byte> cid_bytes;
  for (char c : cid)
    cid_bytes.push_back(std::byte(c));
  bio.ingest(cid_bytes.data(), cid_bytes.size());
  DigestResult dr = bio.finalize_hashed();
  store.addChunk(dr.raw);
  return dr.cid;
}

std::string MerkleTree::hashDirectory(
    const std::vector<std::pair<std::string, std::string>> &entries,
    ChunkStore &store, std::vector<std::string> *nodeCids) {
  std::vector<std::pair<std::string, std::string>> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  BlockIO rootBio;
  std::vector<std::string> nodes;
  for (const auto &e : sorted) {
    std::string nodeCid = hashNode(e.first, e.second, store);
    nodes.push_back(nodeCid);
    std::vector<std::byte> cid_bytes;
    for (char c : nodeCid)
      cid_bytes.push_back(std::byte(c));
    rootBio.ingest(cid_bytes.data(), cid_bytes.size());
  }

  if (nodeCids)
    *nodeCids = nodes;

  DigestResult dr = rootBio.finalize_hashed();
  store.addChunk(dr.raw); // store directory representation itself
  return dr.cid;
}

static std::string buildRecursive(MerkleNode &node, ChunkStore &store) {
  if (!node.isDir)
    return node.cid;

  std::vector<std::pair<std::string, std::string>> entries;
  for (auto &child : node.children) {
    std::string cid = buildRecursive(child.second, store);
    entries.emplace_back(child.first, cid);
    child.second.cid = cid;
  }
  std::vector<std::string> entryCids;
  node.cid = MerkleTree::hashDirectory(entries, store, &entryCids);
  for (size_t i = 0; i < node.children.size() && i < entryCids.size(); ++i) {
    node.children[i].second.entryCid = entryCids[i];
  }
  return node.cid;
}

std::string MerkleTree::buildTree(MerkleNode &root, ChunkStore &store) {
  return buildRecursive(root, store);
}

static bool collectPath(const MerkleNode &node, const std::string &cid,
                        std::vector<std::string> &out) {
  if (!node.isDir) {
    return node.cid == cid;
  }

  for (const auto &child : node.children) {
    if (collectPath(child.second, cid, out)) {
      out.push_back(child.second.entryCid);
      out.push_back(node.cid);
      return true;
    }
  }
  return false;
}

std::vector<std::string> MerkleTree::getProofPath(const MerkleNode &root,
                                                  const std::string &cid) {
  std::vector<std::string> path;
  collectPath(root, cid, path);
  return path;
}

static bool verifyDigest(const std::string &cid,
                         const std::vector<std::byte> &d) {
  BlockIO bio;
  if (!d.empty())
    bio.ingest(d.data(), d.size());
  DigestResult dr = bio.finalize_hashed();
  return dr.cid == cid;
}

bool MerkleTree::verifyProof(const std::string &cid,
                             const std::vector<std::string> &proof,
                             const ChunkStore &store) {
  if (proof.empty())
    return false;

  std::string current = cid;
  for (const auto &stepCid : proof) {
    std::vector<std::byte> data = store.getChunk(stepCid);
    if (data.empty())
      return false;
    if (!verifyDigest(stepCid, data))
      return false;

    std::string dataStr(reinterpret_cast<const char *>(data.data()),
                        data.size());
    if (dataStr.find(current) == std::string::npos)
      return false;
    current = stepCid;
  }
  return true;
}
