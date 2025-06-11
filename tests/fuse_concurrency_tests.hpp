#ifndef SIMPLIDFS_FUSE_CONCURRENCY_TESTS_HPP
#define SIMPLIDFS_FUSE_CONCURRENCY_TESTS_HPP

/**
 * @brief Run the random write portion of the FUSE concurrency tests.
 *
 * This function executes the full random write scenario: creating the
 * test file, launching writer threads, and verifying the result.
 *
 * @return true if the random write test succeeds, false otherwise.
 */
bool run_random_write_test();

/**
 * @brief Run the append portion of the FUSE concurrency tests.
 *
 * The append test launches multiple threads that append to a single file
 * and then verifies the resulting file contents.
 *
 * @return true if the append test succeeds, false otherwise.
 */
bool run_append_test();

#endif // SIMPLIDFS_FUSE_CONCURRENCY_TESTS_HPP
