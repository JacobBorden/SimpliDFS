#include "fuse_stress_test.hpp"
#include "utilities/stress_utils.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Implementation of a simple read/write endurance test for the FUSE adapter.
// The helper writes a deterministic pattern to a file within the mounted
// filesystem and verifies the data during a subsequent read.  The overall test
// is intentionally straightforward to keep I/O throughput high while still
// catching corruption.
//------------------------------------------------------------------------------

// Retrieve mount point from environment or fall back to default.
static std::string get_mount_point() {
    // Allow the mount location to be overridden via environment variable so the
    // same binary can run in different environments (CI, development, etc.).
    const char* env = std::getenv("SIMPLIDFS_CONCURRENCY_MOUNT");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    // Default used when the variable is not set by the wrapper script.
    return std::string("/tmp/myfusemount");
}

bool run_fuse_stress(std::size_t gigabytes) {
    // Use 1 MiB blocks for decent throughput while still catching small errors.
    const std::size_t block_size = 1024 * 1024;
    // Number of blocks needed to reach the requested number of gigabytes.
    const std::size_t blocks_to_write = gigabytes * 1024;

    // Full path of the test file within the mounted filesystem.
    std::string path = get_mount_point() + "/stress_test.dat";

    // Generate the deterministic write pattern once so every block is identical
    // and verification can re-use the same buffer.
    auto pattern = generate_pseudo_random_data(block_size);

    // ---------------------------------------------------------------------
    // Write phase: create the test file and fill it with repeated copies of the
    // pseudo-random pattern.
    // ---------------------------------------------------------------------

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open " << path << " for writing\n";
        return false;
    }
    for (std::size_t i = 0; i < blocks_to_write; ++i) {
        out.write(reinterpret_cast<const char*>(pattern.data()), pattern.size());
    }
    out.close();

    // ---------------------------------------------------------------------
    // Read phase: reopen the file and verify each block against the pattern.
    // ---------------------------------------------------------------------

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << path << " for reading\n";
        return false;
    }
    std::vector<unsigned char> read_buf(block_size);
    std::size_t bit_errors = 0;
    for (std::size_t i = 0; i < blocks_to_write; ++i) {
        // Read the next block from disk and ensure the read succeeded.
        in.read(reinterpret_cast<char*>(read_buf.data()), read_buf.size());
        if (static_cast<std::size_t>(in.gcount()) != block_size) {
            std::cerr << "Short read encountered\n";
            return false;
        }

        // Compare with the expected pattern and accumulate bit errors. The test
        // aborts early if more than one bit differs across the entire data
        // set.
        bit_errors += count_bit_errors(pattern, read_buf);
        if (bit_errors > 1) {
            break;
        }
    }
    in.close();
    std::remove(path.c_str()); // Clean up the temporary file
    // Provide the count for log collection so the CI run can report it.
    std::cout << "BIT_ERRORS:" << bit_errors << std::endl;
    return bit_errors <= 1;
}
