#include "fuse_stress_test.hpp"
#include <cstdlib>

// Entry point for the standalone stress test executable used by CTest.  The
// wrapper script passes the mount point via the SIMPLIDFS_CONCURRENCY_MOUNT
// environment variable so this program only needs to forward the desired size
// in gigabytes to run_fuse_stress().

int main(int argc, char** argv) {
    // Default to ten gigabytes written unless overridden by the first argument.
    std::size_t gb = 10;
    if (argc > 1) {
        gb = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }

    // Delegate the heavy lifting to run_fuse_stress().  The return value maps
    // directly to the program's exit status so CTest can detect failures.
    return run_fuse_stress(gb) ? 0 : 1;
}
