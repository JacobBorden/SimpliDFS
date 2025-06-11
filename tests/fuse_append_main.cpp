#include "fuse_concurrency_tests.hpp"

/**
 * @brief Entry point for the append FUSE concurrency test.
 */
int main() {
    return run_append_test() ? 0 : 1;
}
