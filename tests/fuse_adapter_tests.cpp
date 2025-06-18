#include "utilities/fuse_adapter.h"
#include <gtest/gtest.h>

TEST(FuseAdapterTests, SanitizeOffsetNegative) {
    EXPECT_EQ(sanitize_offset(-5), 0);
}

TEST(FuseAdapterTests, SanitizeOffsetPositive) {
    EXPECT_EQ(sanitize_offset(42), 42);
}
