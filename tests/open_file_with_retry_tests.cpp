#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <fstream>

TEST(OpenFileWithRetry, OpensExistingFile) {
    const std::string path = "retry_existing.txt";
    {
        std::ofstream out(path);
        out << "data";
    }
    std::ifstream in;
    EXPECT_TRUE(openFileWithRetry(path, in, std::ios::in, 2, 10));
    EXPECT_TRUE(in.is_open());
    in.close();
    std::remove(path.c_str());
}

TEST(OpenFileWithRetry, FailsForMissingFile) {
    std::ifstream in;
    EXPECT_FALSE(openFileWithRetry("retry_missing.txt", in, std::ios::in, 1, 10));
    EXPECT_FALSE(in.is_open());
}

