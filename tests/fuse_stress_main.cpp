#include "fuse_stress_test.hpp"
#include <cstdlib>

int main(int argc, char** argv) {
    std::size_t gb = 10;
    if (argc > 1) {
        gb = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
    }
    return run_fuse_stress(gb) ? 0 : 1;
}
