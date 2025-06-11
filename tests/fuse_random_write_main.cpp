#include "fuse_concurrency_tests.hpp"

/**
 * @brief Entry point for the random write FUSE concurrency test.
 */
int main() {
    return run_random_write_test() ? 0 : 1;
}
