#include <gtest/gtest.h>
#include "utilities/merkle_tree.hpp"
#include "utilities/chunk_store.hpp"
#include "utilities/key_manager.hpp"

TEST(MerkleTree, NestedProofVerification) {
    simplidfs::KeyManager::getInstance().initialize();
    ChunkStore store;

    std::vector<std::byte> a{std::byte{'a'}};
    std::vector<std::byte> b{std::byte{'b'}};
    std::vector<std::byte> c{std::byte{'c'}};
    std::string cidA = store.addChunk(a);
    std::string cidB = store.addChunk(b);
    std::string cidC = store.addChunk(c);

    std::string cidDir2 = MerkleTree::hashDirectory({{"fileB", cidB}}, store);
    std::string cidDir1 = MerkleTree::hashDirectory({{"fileA", cidA}, {"dir2", cidDir2}}, store);
    std::string rootCid = MerkleTree::hashDirectory({{"dir1", cidDir1}, {"fileC", cidC}}, store);

    auto proof = MerkleTree::getProofPath(rootCid, cidB);
    ASSERT_FALSE(proof.empty());
    EXPECT_EQ(proof.front(), cidB);
    EXPECT_EQ(proof.back(), rootCid);
    EXPECT_TRUE(MerkleTree::verifyProof(rootCid, cidB, proof));

    EXPECT_TRUE(store.hasChunk(cidDir1));
    EXPECT_TRUE(store.hasChunk(cidDir2));
}
