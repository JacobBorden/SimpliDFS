#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <fstream>
#include <cstdio>

/**
 * @brief Ensure seekpWithRetry positions a valid stream.
 */
TEST(SeekpWithRetry, WorksForValidOffset) {
    const std::string path = "seek_retry_valid.txt";

    // Create a small file to seek within.
    {
        std::ofstream out(path, std::ios::binary);
        out << std::string(20, 'a');
    }

    // Open the file for read/write access and verify it opened correctly.
    std::fstream fs(path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(fs.is_open());

    // Expect the helper to successfully position the stream.
    EXPECT_TRUE(seekpWithRetry(fs, 5));

    // Verify the put pointer ended up at the requested location.
    EXPECT_EQ(static_cast<std::streamoff>(fs.tellp()), 5);

    fs.close();

    // Clean up test artifact.
    std::remove(path.c_str());
}

/**
 * @brief Ensure seekpWithRetry returns false for an unopened stream.
 */
TEST(SeekpWithRetry, FailsForClosedStream) {
    // Intentionally use an unopened stream to trigger failure.
    std::fstream fs;

    // seekpWithRetry should immediately report failure.
    EXPECT_FALSE(seekpWithRetry(fs, 10, 1, 10));
}
