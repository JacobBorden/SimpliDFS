#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <sys/stat.h>
#include <cstdio>

TEST(PreallocateFile, CreatesSpecifiedSize) {
    const std::string path = "prealloc_test.tmp";
    const off_t desiredSize = 1024;
    ASSERT_TRUE(preallocateFile(path, desiredSize));
    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_size, desiredSize);
    std::remove(path.c_str());
}
