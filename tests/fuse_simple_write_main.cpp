#include "fuse_basic_tests.hpp"
/**
 * @brief Entry point for the simple write/read test.
 */
int main() {
    return run_simple_write_read_test() ? 0 : 1;
}
