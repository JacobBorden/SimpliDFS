#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <fstream>

TEST(SeekpWithRetry, SucceedsOnValidStream) {
    const std::string path = "seek_retry.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << std::string(20, 'a');
    }
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(f.is_open());
    EXPECT_TRUE(seekpWithRetry(f, 10));
    EXPECT_EQ(f.tellp(), 10);
    f.close();
    std::remove(path.c_str());
}

TEST(SeekpWithRetry, FailsForClosedStream) {
    std::fstream f;
    EXPECT_FALSE(seekpWithRetry(f, 0, 1, 1));
}
