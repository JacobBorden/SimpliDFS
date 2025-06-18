#include "gtest/gtest.h"
#include "utilities/stress_utils.hpp"

TEST(StressUtils, GenerateSize) {
    auto data = generate_pseudo_random_data(32, 123);
    EXPECT_EQ(data.size(), 32u);
}

TEST(StressUtils, BitErrorsZero) {
    auto data = generate_pseudo_random_data(16, 42);
    EXPECT_EQ(count_bit_errors(data, data), 0u);
}

TEST(StressUtils, BitErrorsOne) {
    std::vector<unsigned char> a{0xFF};
    std::vector<unsigned char> b{0xFE};
    EXPECT_EQ(count_bit_errors(a, b), 1u);
}
