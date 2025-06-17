#include "utilities/stress_utils.hpp"
#include <bitset>
#include <random>

std::vector<unsigned char> generate_pseudo_random_data(std::size_t size,
                                                       unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<unsigned char> buffer(size);
    for (std::size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<unsigned char>(dist(rng));
    }
    return buffer;
}

std::size_t count_bit_errors(const std::vector<unsigned char>& expected,
                             const std::vector<unsigned char>& actual) {
    std::size_t errors = 0;
    std::size_t min_size = std::min(expected.size(), actual.size());
    for (std::size_t i = 0; i < min_size; ++i) {
        unsigned char diff = expected[i] ^ actual[i];
        errors += std::bitset<8>(diff).count();
    }
    if (expected.size() > actual.size()) {
        errors += (expected.size() - actual.size()) * 8;
    } else if (actual.size() > expected.size()) {
        errors += (actual.size() - expected.size()) * 8;
    }
    return errors;
}
