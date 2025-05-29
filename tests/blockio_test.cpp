#include "gtest/gtest.h"
#include "utilities/blockio.hpp" // Adjust path as necessary
#include <vector>
#include <cstddef> // For std::byte
#include <numeric> // For std::iota
#include <algorithm> // For std::equal

// Helper function to create a vector of bytes with sequential values
std::vector<std::byte> create_byte_vector(size_t size) {
    std::vector<std::byte> vec(size);
    // Initialize with distinct values for better testing
    for (size_t i = 0; i < size; ++i) {
        vec[i] = std::byte(i % 256); 
    }
    return vec;
}

TEST(BlockIOTest, IngestEmpty) {
    BlockIO bio;
    std::vector<std::byte> empty_data;
    bio.ingest(empty_data.data(), empty_data.size());
    std::vector<std::byte> result = bio.finalize_raw();
    EXPECT_TRUE(result.empty());
}

TEST(BlockIOTest, IngestSingleByte) {
    BlockIO bio;
    std::vector<std::byte> single_byte_data = {std::byte{77}};
    bio.ingest(single_byte_data.data(), single_byte_data.size());
    std::vector<std::byte> result = bio.finalize_raw();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], std::byte{77});
}

TEST(BlockIOTest, Ingest64KiB) {
    BlockIO bio;
    const size_t data_size = 64 * 1024; // 64 KiB
    std::vector<std::byte> data = create_byte_vector(data_size);
    bio.ingest(data.data(), data.size());
    std::vector<std::byte> result = bio.finalize_raw();
    ASSERT_EQ(result.size(), data_size);
    EXPECT_TRUE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST(BlockIOTest, Ingest4MiB) {
    BlockIO bio;
    const size_t data_size = 4 * 1024 * 1024; // 4 MiB
    std::vector<std::byte> data = create_byte_vector(data_size);
    bio.ingest(data.data(), data.size());
    std::vector<std::byte> result = bio.finalize_raw();
    ASSERT_EQ(result.size(), data_size);
    EXPECT_TRUE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST(BlockIOTest, IngestMultipleChunks) {
    BlockIO bio;
    std::vector<std::byte> full_expected_data;

    // Chunk 1: 10 bytes
    std::vector<std::byte> chunk1(10);
    for(size_t i=0; i<10; ++i) chunk1[i] = std::byte(i);
    bio.ingest(chunk1.data(), chunk1.size());
    full_expected_data.insert(full_expected_data.end(), chunk1.begin(), chunk1.end());

    // Chunk 2: Empty
    std::vector<std::byte> chunk2;
    bio.ingest(chunk2.data(), chunk2.size());
    // No change to full_expected_data needed

    // Chunk 3: 20 bytes
    std::vector<std::byte> chunk3(20);
    for(size_t i=0; i<20; ++i) chunk3[i] = std::byte(10 + i); // Continue sequence
    bio.ingest(chunk3.data(), chunk3.size());
    full_expected_data.insert(full_expected_data.end(), chunk3.begin(), chunk3.end());

    std::vector<std::byte> result_bio = bio.finalize_raw();
    
    ASSERT_EQ(result_bio.size(), full_expected_data.size());
    EXPECT_TRUE(std::equal(result_bio.begin(), result_bio.end(), full_expected_data.begin()));
}

// Assuming tests/tests_main.cpp already includes main for Google Test.
// If not, and this is the only test file or the designated file for main:
// int main(int argc, char **argv) {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
