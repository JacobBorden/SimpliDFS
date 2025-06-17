#include "fuse_stress_test.hpp"
#include "utilities/stress_utils.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

// Retrieve mount point from environment or fall back to a temporary default.
static std::string get_mount_point() {
    const char* env = std::getenv("SIMPLIDFS_CONCURRENCY_MOUNT");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    return std::string("/tmp/myfusemount");
}

bool run_fuse_stress(std::size_t gigabytes) {
    // We write one megabyte blocks repeatedly to reach the desired total size.
    const std::size_t block_size = 1024 * 1024; // 1 MB
    const std::size_t blocks_to_write = gigabytes * 1024;

    // File path within the mounted filesystem.
    std::string path = get_mount_point() + "/stress_test.dat";

    // Generate a deterministic pattern for both writing and verification.
    auto pattern = generate_pseudo_random_data(block_size);

    // Write the pattern repeatedly to the file.
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open " << path << " for writing\n";
        return false;
    }
    for (std::size_t i = 0; i < blocks_to_write; ++i) {
        out.write(reinterpret_cast<const char*>(pattern.data()), pattern.size());
    }
    out.close();

    // Read the file back and accumulate bit errors.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open " << path << " for reading\n";
        return false;
    }
    std::vector<unsigned char> read_buf(block_size);
    std::size_t bit_errors = 0;
    for (std::size_t i = 0; i < blocks_to_write; ++i) {
        in.read(reinterpret_cast<char*>(read_buf.data()), read_buf.size());
        if (static_cast<std::size_t>(in.gcount()) != block_size) {
            std::cerr << "Short read encountered\n";
            return false;
        }

        // Compare read data with the expected pattern.
        bit_errors += count_bit_errors(pattern, read_buf);

        // No need to continue once we exceed the threshold.
        if (bit_errors > 1) {
            break;
        }
    }
    in.close();

    // Clean up the test file from the mount point.
    std::remove(path.c_str());

    // Emit the final bit error count for logging.
    std::cout << "BIT_ERRORS:" << bit_errors << std::endl;

    // The pipeline will fail if more than one bit was corrupted.
    return bit_errors <= 1;
}
