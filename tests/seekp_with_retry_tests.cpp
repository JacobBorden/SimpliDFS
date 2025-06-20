#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <fstream>
#include <cstdio>

/**
 * @brief Ensure seekpWithRetry positions a valid stream.
 */
TEST(SeekpWithRetry, WorksForValidOffset) {
    const std::string path = "seek_retry_valid.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << std::string(20, 'a');
    }
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(fs.is_open());
    EXPECT_TRUE(seekpWithRetry(fs, 5));
    EXPECT_EQ(static_cast<std::streamoff>(fs.tellp()), 5);
    fs.close();
    std::remove(path.c_str());
}

/**
 * @brief Ensure seekpWithRetry returns false for an unopened stream.
 */
TEST(SeekpWithRetry, FailsForClosedStream) {
    std::fstream fs; // not opened
    EXPECT_FALSE(seekpWithRetry(fs, 10, 1, 10));
}
