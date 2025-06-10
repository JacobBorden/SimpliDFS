#include <gtest/gtest.h>
#include "utilities/merkle_tree.hpp"
#include "utilities/chunk_store.hpp"
#include "utilities/blockio.hpp"
#include "utilities/key_manager.hpp"
#include <algorithm>

/**
 * @brief Verify hashDirectory determinism and chunk storage.
 */
TEST(MerkleTree, HashDirectoryDeterministic) {
    // Initialize libsodium for BlockIO via KeyManager
    simplidfs::KeyManager::getInstance().initialize();
    ChunkStore store;

    // Same directory entries in different orders
    std::vector<std::pair<std::string,std::string>> entriesA{{"b","cid2"},{"a","cid1"}};
    std::vector<std::pair<std::string,std::string>> entriesB{{"a","cid1"},{"b","cid2"}};

    std::string cid1 = MerkleTree::hashDirectory(entriesA, store);
    std::string cid2 = MerkleTree::hashDirectory(entriesB, store);
    EXPECT_EQ(cid1, cid2); // order should not matter
    EXPECT_TRUE(store.hasChunk(cid1));

    // Reconstruct expected digest manually
    std::vector<std::pair<std::string,std::string>> sorted{{"a","cid1"},{"b","cid2"}};
    BlockIO bio;
    for (const auto& e : sorted) {
        std::vector<std::byte> name_bytes;
        for(char c : e.first) name_bytes.push_back(std::byte(c));
        bio.ingest(name_bytes.data(), name_bytes.size());
        std::vector<std::byte> hash_bytes;
        for(char c : e.second) hash_bytes.push_back(std::byte(c));
        bio.ingest(hash_bytes.data(), hash_bytes.size());
    }
    DigestResult dr = bio.finalize_hashed();
    EXPECT_EQ(dr.cid, cid1);
    EXPECT_EQ(store.getChunk(cid1), dr.raw);
}
