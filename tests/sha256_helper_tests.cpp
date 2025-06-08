#include <gtest/gtest.h>
#include "fuse_concurrency_test_utils.hpp"
#include <sodium.h>
#include <sstream>
#include <iomanip>

TEST(SHA256Helper, ReturnsExpectedDigest) {
    ASSERT_GE(sodium_init(), 0);
    auto digest = compute_sha256("test");
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char c : digest) {
        oss << std::setw(2) << static_cast<int>(c);
    }
    EXPECT_EQ(oss.str(), "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08");
}
