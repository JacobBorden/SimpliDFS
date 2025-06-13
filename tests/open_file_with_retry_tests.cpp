#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <fstream>
#include <thread>
#include <chrono>

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

TEST(OpenFileWithRetry, SucceedsAfterFileCreated) {
    const std::string path = "retry_delayed.txt";
    std::thread creator([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ofstream out(path);
        out << "data";
    });
    std::ifstream in;
    EXPECT_TRUE(openFileWithRetry(path, in, std::ios::in, 5, 20));
    EXPECT_TRUE(in.is_open());
    std::string contents;
    std::getline(in, contents);
    EXPECT_EQ(contents, "data");
    in.close();
    creator.join();
    std::remove(path.c_str());
}

