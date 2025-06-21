#include "utilities/chunk_store.hpp"
#include "utilities/key_manager.hpp"
#include "utilities/merkle_tree.hpp"
#include <gtest/gtest.h>

TEST(MerkleTree, NestedProofVerification) {
  simplidfs::KeyManager::getInstance().initialize();
  ChunkStore store;

  // Build leaf chunks
  std::vector<std::byte> dataA{std::byte{'a'}};
  std::vector<std::byte> dataB{std::byte{'b'}};
  std::string cidA = store.addChunk(dataA);
  std::string cidB = store.addChunk(dataB);

  MerkleNode fileA;
  fileA.isDir = false;
  fileA.cid = cidA;

  MerkleNode fileB;
  fileB.isDir = false;
  fileB.cid = cidB;

  MerkleNode subdir;
  subdir.isDir = true;
  subdir.children.push_back({"inner", fileB});

  MerkleNode root;
  root.isDir = true;
  root.children.push_back({"a", fileA});
  root.children.push_back({"dir", subdir});

  std::string rootCid = MerkleTree::buildTree(root, store);
  auto proof = MerkleTree::getProofPath(root, cidB);
  ASSERT_EQ(proof.back(), rootCid);
  EXPECT_TRUE(MerkleTree::verifyProof(cidB, proof, store));
}
