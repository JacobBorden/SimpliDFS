#ifndef SIMPLIDFS_FUSE_STRESS_TEST_HPP
#define SIMPLIDFS_FUSE_STRESS_TEST_HPP
#include <cstddef>

/**
 * @brief Execute a large FUSE write/read stress test.
 *
 * The test writes a pattern to a file within the mounted filesystem
 * and verifies it after reading it back. If more than a single bit
 * differs across the written data, the function returns false.
 *
 * @param gigabytes Amount of data to write in gigabytes.
 * @return true on success, false if excessive bit errors are detected.
 */
bool run_fuse_stress(std::size_t gigabytes = 10);

#endif // SIMPLIDFS_FUSE_STRESS_TEST_HPP
