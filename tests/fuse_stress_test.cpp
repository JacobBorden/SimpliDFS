#include "fuse_stress_test.hpp"
#include "utilities/stress_utils.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

// Retrieve mount point from environment or fall back to default.
static std::string get_mount_point() {
    const char* env = std::getenv("SIMPLIDFS_CONCURRENCY_MOUNT");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    return std::string("/tmp/myfusemount");
}

bool run_fuse_stress(std::size_t gigabytes) {
    const std::size_t block_size = 1024 * 1024; // 1MB blocks
    const std::size_t blocks_to_write = gigabytes * 1024;
    std::string path = get_mount_point() + "/stress_test.dat";

    auto pattern = generate_pseudo_random_data(block_size);

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open " << path << " for writing\n";
        return false;
    }
    for (std::size_t i = 0; i < blocks_to_write; ++i) {
        out.write(reinterpret_cast<const char*>(pattern.data()), pattern.size());
    }
    out.close();

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
        bit_errors += count_bit_errors(pattern, read_buf);
        if (bit_errors > 1) {
            break;
        }
    }
    in.close();
    std::remove(path.c_str());
    std::cout << "BIT_ERRORS:" << bit_errors << std::endl;
    return bit_errors <= 1;
}
