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

  // Reconstruct expected digest manually
  std::vector<std::pair<std::string, std::string>> sorted{{"a", "cid1"},
                                                          {"b", "cid2"}};
  BlockIO bio;
  for (const auto &e : sorted) {
    uint32_t nlen = static_cast<uint32_t>(e.first.size());
    bio.ingest(reinterpret_cast<std::byte *>(&nlen), sizeof(nlen));
    bio.ingest(reinterpret_cast<const std::byte *>(e.first.data()), nlen);
    uint32_t clen = static_cast<uint32_t>(e.second.size());
    bio.ingest(reinterpret_cast<std::byte *>(&clen), sizeof(clen));
    bio.ingest(reinterpret_cast<const std::byte *>(e.second.data()), clen);
  }
  DigestResult dr = bio.finalize_hashed();
  EXPECT_EQ(dr.cid, cid1);
  EXPECT_EQ(store.getChunk(cid1), dr.raw);
}

TEST(MerkleTree, ProofNestedDirectories) {
  simplidfs::KeyManager::getInstance().initialize();
  ChunkStore store;

  MerkleTree::DirEntry root;
  root.name = "root";
  root.isFile = false;
  MerkleTree::DirEntry file1;
  file1.name = "file1";
  file1.isFile = true;
  file1.data = {std::byte{'A'}};
  MerkleTree::DirEntry file2;
  file2.name = "file2";
  file2.isFile = true;
  file2.data = {std::byte{'B'}};
  MerkleTree::DirEntry subdir;
  subdir.name = "sub";
  subdir.isFile = false;
  subdir.children = {file2};
  root.children = {file1, subdir};

  std::unordered_map<std::string, std::string> parents;
  std::string rootCid = MerkleTree::buildTree(root, store, parents);

  std::vector<std::byte> tmp = {std::byte{'B'}};
  std::string file2Cid = store.addChunk(tmp);

  auto path = MerkleTree::proofPath(file2Cid, parents);
  ASSERT_FALSE(path.empty());
  EXPECT_EQ(path.back(), rootCid);
  EXPECT_TRUE(MerkleTree::verifyProof(path, store));
}
