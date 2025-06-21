#include "utilities/blockio.hpp"
#include "utilities/chunk_store.hpp"
#include "utilities/key_manager.hpp"
#include "utilities/merkle_tree.hpp"
#include <algorithm>
#include <gtest/gtest.h>

/**
 * @brief Verify hashDirectory determinism and chunk storage.
 */
TEST(MerkleTree, HashDirectoryDeterministic) {
  // Initialize libsodium for BlockIO via KeyManager
  simplidfs::KeyManager::getInstance().initialize();
  ChunkStore store;

  // Same directory entries in different orders
  std::vector<std::pair<std::string, std::string>> entriesA{{"b", "cid2"},
                                                            {"a", "cid1"}};
  std::vector<std::pair<std::string, std::string>> entriesB{{"a", "cid1"},
                                                            {"b", "cid2"}};

  std::string cid1 = MerkleTree::hashDirectory(entriesA, store);
  std::string cid2 = MerkleTree::hashDirectory(entriesB, store);
  EXPECT_EQ(cid1, cid2); // order should not matter
  EXPECT_TRUE(store.hasChunk(cid1));

  // Reconstruct expected digest manually with intermediate nodes
  std::vector<std::pair<std::string, std::string>> sorted{{"a", "cid1"},
                                                          {"b", "cid2"}};
  std::vector<std::string> nodeCids;
  std::vector<std::byte> rootRaw;
  BlockIO rootBio;
  for (const auto &e : sorted) {
    BlockIO nodeBio;
    std::vector<std::byte> name_bytes;
    for (char c : e.first)
      name_bytes.push_back(std::byte(c));
    nodeBio.ingest(name_bytes.data(), name_bytes.size());
    std::vector<std::byte> cid_bytes;
    for (char c : e.second)
      cid_bytes.push_back(std::byte(c));
    nodeBio.ingest(cid_bytes.data(), cid_bytes.size());
    DigestResult nd = nodeBio.finalize_hashed();
    nodeCids.push_back(nd.cid);
    std::vector<std::byte> cidb;
    for (char c : nd.cid)
      cidb.push_back(std::byte(c));
    rootBio.ingest(cidb.data(), cidb.size());
  }
  DigestResult dr = rootBio.finalize_hashed();
  EXPECT_EQ(dr.cid, cid1);
  EXPECT_EQ(store.getChunk(cid1), dr.raw);
}
