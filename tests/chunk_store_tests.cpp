#include "gtest/gtest.h"
#include "utilities/chunk_store.hpp"
#include <string>

TEST(ChunkStoreTest, AddAndRetrieveChunk) {
    ChunkStore store;
    std::string data = "hello";
    std::vector<std::byte> bytes;
    for(char c : data) bytes.push_back(std::byte(c));
    std::string cid = store.addChunk(bytes);
    EXPECT_FALSE(cid.empty());
    EXPECT_TRUE(store.hasChunk(cid));
    std::vector<std::byte> out = store.getChunk(cid);
    std::string out_str(reinterpret_cast<const char*>(out.data()), out.size());
    EXPECT_EQ(out_str, data);
}
