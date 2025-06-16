#include "fuse_basic_tests.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <thread>

/**
 * @brief Retrieve the FUSE mount point.
 *
 * The mount point is provided by the wrapper script via the
 * SIMPLIDFS_CONCURRENCY_MOUNT environment variable. If the
 * variable is not set, a fallback location is used so that
 * the tests can run manually.
 *
 * @return Path to the mount point to interact with.
 */
static std::string get_mount_point() {
    const char *env_path = std::getenv("SIMPLIDFS_CONCURRENCY_MOUNT");
    if (env_path && env_path[0] != '\0') {
        return std::string(env_path);
    }
    return std::string("/tmp/myfusemount");
}

/**
 * @brief Generate a timestamp for logging.
 *
 * Each log line is prefixed with a human readable timestamp so
 * debugging output from the tests can be correlated easily.
 *
 * @return Formatted timestamp string.
 */
static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm bt{};
    localtime_r(&t, &bt);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", bt.tm_hour, bt.tm_min, bt.tm_sec);
    return std::string(buffer);
}

/**
 * @brief Check that the mount point exists and is accessible.
 *
 * @return true if the mount point is ready for use.
 */
static bool check_mount_point_ready() {
    const std::string mount = get_mount_point();
    struct stat sb{};
    std::cout << "[FUSE BASIC LOG " << timestamp() << "] Checking mount point " << mount << std::endl;
    if (stat(mount.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
        if (access(mount.c_str(), R_OK | W_OK | X_OK) == 0) {
            std::cout << "[FUSE BASIC LOG " << timestamp() << "] Mount point accessible." << std::endl;
            return true;
        }
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Mount point lacks required permissions." << std::endl;
        return false;
    }
    std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Mount point missing or not a directory." << std::endl;
    return false;
}

/**
 * @brief Basic check that the FUSE mount succeeds.
 *
 * This simply verifies that the mount point is reachable.
 *
 * @return true if the mount appears operational.
 */
bool run_mount_test() {
    std::cout << "[FUSE BASIC LOG " << timestamp() << "] MountTest starting." << std::endl;
    bool ok = check_mount_point_ready();
    std::cout << "[FUSE BASIC LOG " << timestamp() << "] MountTest " << (ok ? "succeeded" : "failed") << std::endl;
    return ok;
}

/**
 * @brief Write a file and read it back to verify contents.
 *
 * The test writes "hello world" to a file inside the mount point
 * and then reads the file back to ensure the contents match.
 *
 * @return true if the file contents round-trip correctly.
 */
bool run_simple_write_read_test() {
    if (!check_mount_point_ready()) {
        return false;
    }
    const std::string mount = get_mount_point();
    const std::string file_path = mount + "/write_read.txt";
    const std::string expected = "hello world";
    std::ofstream out(file_path);
    if (!out.is_open()) {
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Failed to open " << file_path << " for writing." << std::endl;
        return false;
    }
    out << expected;
    out.close();

    std::ifstream in(file_path);
    if (!in.is_open()) {
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Failed to open " << file_path << " for reading." << std::endl;
        return false;
    }
    std::string actual;
    std::getline(in, actual);
    in.close();
    std::remove(file_path.c_str());

    if (actual == expected) {
        std::cout << "[FUSE BASIC LOG " << timestamp() << "] WriteReadTest succeeded." << std::endl;
        return true;
    }
    std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Content mismatch: '" << actual << "'" << std::endl;
    return false;
}

/**
 * @brief Write to a file and append more data, verifying the final result.
 *
 * The test creates a file with "hello" then reopens it in append mode
 * to add " world". The resulting file is checked for the combined
 * string "hello world".
 *
 * @return true if the append operation works correctly.
 */
bool run_simple_append_test() {
    if (!check_mount_point_ready()) {
        return false;
    }
    const std::string mount = get_mount_point();
    const std::string file_path = mount + "/append_test.txt";
    const std::string first_part = "hello";
    const std::string second_part = " world";
    const std::string expected = first_part + second_part;

    // Write the initial portion
    std::ofstream out(file_path);
    if (!out.is_open()) {
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Failed to open " << file_path << " for initial write." << std::endl;
        return false;
    }
    out << first_part;
    out.close();

    // Append the second portion
    std::ofstream append_out(file_path, std::ios::app);
    if (!append_out.is_open()) {
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Failed to reopen " << file_path << " for append." << std::endl;
        return false;
    }
    append_out << second_part;
    append_out.close();

    // Read back the file
    std::ifstream in(file_path);
    if (!in.is_open()) {
        std::cerr << "[FUSE BASIC LOG " << timestamp() << "] Failed to open " << file_path << " for verification." << std::endl;
        return false;
    }
    std::string actual;
    std::getline(in, actual);
    in.close();
    std::remove(file_path.c_str());

    if (actual == expected) {
        std::cout << "[FUSE BASIC LOG " << timestamp() << "] AppendTest succeeded." << std::endl;
        return true;
    }
    std::cerr << "[FUSE BASIC LOG " << timestamp() << "] AppendTest mismatch: '" << actual << "'" << std::endl;
    return false;
}


