#include "utilities/merkle_tree.hpp"
#include "utilities/blockio.hpp"
#include <algorithm>
#include <cstring>

std::string MerkleTree::hashDirectory(
    const std::vector<std::pair<std::string, std::string>> &entries,
    ChunkStore &store) {
  std::vector<std::pair<std::string, std::string>> sorted = entries;
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });

  BlockIO bio;
  for (const auto &e : sorted) {
    uint32_t nlen = static_cast<uint32_t>(e.first.size());
    bio.ingest(reinterpret_cast<std::byte *>(&nlen), sizeof(nlen));
    if (nlen)
      bio.ingest(reinterpret_cast<const std::byte *>(e.first.data()), nlen);

    uint32_t clen = static_cast<uint32_t>(e.second.size());
    bio.ingest(reinterpret_cast<std::byte *>(&clen), sizeof(clen));
    if (clen)
      bio.ingest(reinterpret_cast<const std::byte *>(e.second.data()), clen);
  }
  DigestResult dr = bio.finalize_hashed();
  store.addChunk(dr.raw); // store directory representation itself
  return dr.cid;
}

bool MerkleTree::parseDirectory(
    const std::vector<std::byte> &raw,
    std::vector<std::pair<std::string, std::string>> &entries) {
  entries.clear();
  size_t offset = 0;
  while (offset + sizeof(uint32_t) * 2 <= raw.size()) {
    uint32_t nlen = 0;
    std::memcpy(&nlen, raw.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    if (offset + nlen + sizeof(uint32_t) > raw.size())
      return false;
    std::string name(reinterpret_cast<const char *>(raw.data() + offset), nlen);
    offset += nlen;
    uint32_t clen = 0;
    std::memcpy(&clen, raw.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    if (offset + clen > raw.size())
      return false;
    std::string cid(reinterpret_cast<const char *>(raw.data() + offset), clen);
    offset += clen;
    entries.emplace_back(name, cid);
  }
  return offset == raw.size();
}

static std::string
buildTreeInternal(const MerkleTree::DirEntry &node, ChunkStore &store,
                  std::unordered_map<std::string, std::string> &parents) {
  if (node.isFile) {
    std::string cid = store.addChunk(node.data);
    return cid;
  }
  std::vector<std::pair<std::string, std::string>> childPairs;
  for (const auto &c : node.children) {
    std::string childCid = buildTreeInternal(c, store, parents);
    childPairs.emplace_back(c.name, childCid);
    parents[childCid] = ""; // placeholder
  }
  std::string cid = MerkleTree::hashDirectory(childPairs, store);
  for (const auto &p : childPairs) {
    parents[p.second] = cid;
  }
  return cid;
}

std::string
MerkleTree::buildTree(const DirEntry &root, ChunkStore &store,
                      std::unordered_map<std::string, std::string> &parents) {
  parents.clear();
  return buildTreeInternal(root, store, parents);
}

std::vector<std::string> MerkleTree::proofPath(
    const std::string &cid,
    const std::unordered_map<std::string, std::string> &parents) {
  std::vector<std::string> path;
  std::string current = cid;
  path.push_back(current);
  while (true) {
    auto it = parents.find(current);
    if (it == parents.end() || it->second.empty())
      break;
    current = it->second;
    path.push_back(current);
  }
  return path;
}

bool MerkleTree::verifyProof(const std::vector<std::string> &path,
                             const ChunkStore &store) {
  if (path.empty())
    return false;

  auto verifyChunk = [&](const std::string &cid) -> bool {
    auto raw = store.getChunk(cid);
    BlockIO b;
    if (!raw.empty())
      b.ingest(raw.data(), raw.size());
    DigestResult dr = b.finalize_hashed();
    return dr.cid == cid;
  };

  if (!verifyChunk(path[0]))
    return false;

  for (size_t i = 1; i < path.size(); ++i) {
    const std::string &parentCid = path[i];
    const std::string &childCid = path[i - 1];
    auto rawParent = store.getChunk(parentCid);
    std::vector<std::pair<std::string, std::string>> entries;
    if (!parseDirectory(rawParent, entries))
      return false;

    bool found = false;
    for (const auto &e : entries) {
      if (e.second == childCid) {
        found = true;
        break;
      }
    }
    if (!found)
      return false;

    ChunkStore tmp;
    std::string calc = MerkleTree::hashDirectory(entries, tmp);
    if (calc != parentCid)
      return false;

    if (!verifyChunk(parentCid))
      return false;
  }
  return true;
}
