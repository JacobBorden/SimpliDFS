#ifndef SIMPLIDFS_STRESS_UTILS_HPP
#define SIMPLIDFS_STRESS_UTILS_HPP

#include <cstddef>
#include <vector>

// Utilities for the FUSE stress tests.  Keeping these helpers small and
// header-only allows the tests to remain independent of the rest of the project
// while still sharing a single implementation across multiple stress binaries.

/**
 * @brief Generate deterministic pseudo-random data.
 *
 * This helper uses a simple mersenne twister engine so runs are
 * reproducible across platforms.
 *
 * @param size  Number of bytes to generate.
 * @param seed  Seed value for the generator.
 * @return Byte vector containing the generated data.
 */
std::vector<unsigned char> generate_pseudo_random_data(std::size_t size,
                                                       unsigned int seed = 0xDEADBEEF);

/**
 * @brief Count differing bits between two buffers.
 *
 * @param expected Buffer with expected bytes.
 * @param actual   Buffer with bytes read back.
 * @return Number of bit positions that differ.
 */
std::size_t count_bit_errors(const std::vector<unsigned char>& expected,
                             const std::vector<unsigned char>& actual);

#endif // SIMPLIDFS_STRESS_UTILS_HPP
