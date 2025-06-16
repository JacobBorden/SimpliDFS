#ifndef SIMPLIDFS_FUSE_BASIC_TESTS_HPP
#define SIMPLIDFS_FUSE_BASIC_TESTS_HPP

/**
 * @brief Basic check that the FUSE mount succeeds.
 *
 * @return true if the mount appears operational.
 */
bool run_mount_test();

/**
 * @brief Write a file and read it back to verify contents.
 *
 * @return true if the file contents round-trip correctly.
 */
bool run_simple_write_read_test();

/**
 * @brief Write to a file and append more data.
 *
 * @return true if the append operation works correctly.
 */
bool run_simple_append_test();

#endif // SIMPLIDFS_FUSE_BASIC_TESTS_HPP
