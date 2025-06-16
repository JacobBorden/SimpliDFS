#include "fuse_basic_tests.hpp"
/**
 * @brief Entry point for the basic mount test.
 */
int main() {
    return run_mount_test() ? 0 : 1;
}
