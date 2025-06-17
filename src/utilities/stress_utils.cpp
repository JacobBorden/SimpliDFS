#include "utilities/stress_utils.hpp"
#include <bitset>
#include <random>

std::vector<unsigned char> generate_pseudo_random_data(std::size_t size,
                                                       unsigned int seed) {
    // Use a deterministic Mersenne Twister so tests are reproducible.
    std::mt19937 rng(seed);

    // Generate bytes uniformly across the full 0-255 range.
    std::uniform_int_distribution<int> dist(0, 255);

    // Preallocate the output buffer to the requested size.
    std::vector<unsigned char> buffer(size);

    // Fill the buffer one byte at a time.
    for (std::size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<unsigned char>(dist(rng));
    }

    return buffer;
}

std::size_t count_bit_errors(const std::vector<unsigned char>& expected,
                             const std::vector<unsigned char>& actual) {
    // Track the total number of differing bits.
    std::size_t errors = 0;

    // Only iterate over bytes present in both buffers.
    std::size_t min_size = std::min(expected.size(), actual.size());
    for (std::size_t i = 0; i < min_size; ++i) {
        // XOR reveals all differing bits in the current byte.
        unsigned char diff = expected[i] ^ actual[i];

        // Count the number of set bits in the diff.
        errors += std::bitset<8>(diff).count();
    }

    // Account for any extra bytes in the longer buffer.
    if (expected.size() > actual.size()) {
        errors += (expected.size() - actual.size()) * 8;
    } else if (actual.size() > expected.size()) {
        errors += (actual.size() - expected.size()) * 8;
    }

    return errors;
}
