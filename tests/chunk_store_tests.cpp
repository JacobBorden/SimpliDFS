#include "gtest/gtest.h"
#include "utilities/chunk_store.hpp"
#include "utilities/filesystem.h"
#include <string>
#include <vector>

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

TEST(ChunkStoreTest, GarbageCollectUnreferenced) {
    ChunkStore store;
    FileSystem fs;

    // Add three chunks
    std::vector<std::byte> bytesA(5, std::byte{'a'});
    std::vector<std::byte> bytesB(5, std::byte{'b'});
    std::vector<std::byte> bytesC(5, std::byte{'c'});
    std::string cid1 = store.addChunk(bytesA);
    std::string cid2 = store.addChunk(bytesB);
    std::string cid3 = store.addChunk(bytesC);

    // Reference cid1 and cid2 via filesystem xattrs
    fs.createFile("f1");
    fs.setXattr("f1", "user.cid", cid1);
    fs.createFile("f2");
    fs.setXattr("f2", "user.cid", cid2);

    auto referenced = fs.getAllCids();

    auto statsDry = store.garbageCollect(referenced, true);
    EXPECT_EQ(statsDry.reclaimableChunks, 1u);
    EXPECT_EQ(statsDry.freedChunks, 0u);

    auto statsLive = store.garbageCollect(referenced, false);
    EXPECT_EQ(statsLive.freedChunks, 1u);
    EXPECT_FALSE(store.hasChunk(cid3));
}
