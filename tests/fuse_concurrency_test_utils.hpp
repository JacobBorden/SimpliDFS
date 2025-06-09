#pragma once
#ifndef SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
#define SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP

#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <array>
#include <sodium.h>
#include <iostream> // For std::cerr
#include <cstring>  // For strerror
#include <cerrno>   // For errno

/**
 * @brief Preallocate a file to ensure writes beyond EOF succeed.
 *
 * This helper creates or opens the file at @p path and expands it to
 * @p size bytes. It is primarily used by FuseConcurrencyTest to avoid
 * failures when multiple threads seek past the end of the file.
 *
 * @param path File to extend.
 * @param size Desired size in bytes.
 * @return true on success, false otherwise.
 */
inline bool preallocateFile(const std::string& path, off_t size) {
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd < 0) {
        std::cerr << "preallocateFile Error: Failed to open file '" << path << "': " << strerror(errno) << std::endl;
        return false;
    }

    bool success = true; // Assume success initially

#ifdef __linux__
    // Attempt posix_fallocate first on Linux
    int fallocate_ret = posix_fallocate(fd, 0, size);
    if (fallocate_ret == 0) {
        // posix_fallocate succeeded
    } else {
        // posix_fallocate failed, log it and try ftruncate as a primary fallback
        std::cerr << "preallocateFile Info: posix_fallocate for '" << path << "' to size " << size
                  << " failed: " << strerror(fallocate_ret) // posix_fallocate returns errno value directly
                  << ". Attempting ftruncate and manual write." << std::endl;

        if (ftruncate(fd, size) != 0) {
            std::cerr << "preallocateFile Error: ftruncate for '" << path << "' to size " << size
                      << " failed: " << strerror(errno) << std::endl;
            success = false;
            // Even if ftruncate fails, attempt the manual write as a last resort if size > 0
        }

        // Fallback to manual extension if ftruncate failed or if we want to be absolutely sure (e.g. for older systems or specific FS)
        // This is particularly important if size is 0, as lseek to -1 is invalid.
        if (success && size > 0) { // Only try seek/write if ftruncate seemed to work and size is positive
            if (lseek(fd, size - 1, SEEK_SET) == -1) {
                std::cerr << "preallocateFile Error: lseek for '" << path << "' to offset " << (size - 1)
                          << " failed: " << strerror(errno) << std::endl;
                success = false;
            } else {
                if (::write(fd, "", 1) != 1) {
                    std::cerr << "preallocateFile Error: write of last byte for '" << path << "' at offset " << (size - 1)
                              << " failed: " << strerror(errno) << std::endl;
                    success = false;
                }
            }
        } else if (size == 0 && success) {
            // If size is 0, ftruncate should have already set it. No need for seek/write.
            // If ftruncate failed for size 0, success would be false.
        } else if (!success && size > 0) {
             std::cerr << "preallocateFile Info: Skipping manual extension for '" << path << "' due to prior ftruncate failure." << std::endl;
        }
    }
#else // Non-Linux systems (e.g., macOS)
    // On non-Linux, try ftruncate first
    if (ftruncate(fd, size) != 0) {
        std::cerr << "preallocateFile Error: ftruncate for '" << path << "' to size " << size
                  << " failed: " << strerror(errno) << std::endl;
        success = false;
    }

    // Fallback to manual extension if ftruncate failed or if it's the standard way (e.g. on macOS)
    // This is particularly important if size is 0, as lseek to -1 is invalid.
    if (success && size > 0) { // Only try seek/write if ftruncate seemed to work and size is positive
        if (lseek(fd, size - 1, SEEK_SET) == -1) {
            std::cerr << "preallocateFile Error: lseek for '" << path << "' to offset " << (size - 1)
                      << " failed: " << strerror(errno) << std::endl;
            success = false;
        } else {
            if (::write(fd, "", 1) != 1) {
                std::cerr << "preallocateFile Error: write of last byte for '" << path << "' at offset " << (size -1)
                          << " failed: " << strerror(errno) << std::endl;
                success = false;
            }
        }
    } else if (size == 0 && success) {
        // If size is 0, ftruncate should have already set it.
    } else if (!success && size > 0) {
        std::cerr << "preallocateFile Info: Skipping manual extension for '" << path << "' due to prior ftruncate failure." << std::endl;
    }
#endif

    // Post-preallocation size check
    if (success) { // Only check size if allocation attempts were thought to be successful
        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            std::cerr << "preallocateFile Error: fstat failed for file '" << path << "': " << strerror(errno) << std::endl;
            success = false;
        } else {
            if (sb.st_size != size) {
                std::cerr << "preallocateFile Error: Size mismatch for file '" << path
                          << "'. Expected: " << size << ", Actual: " << sb.st_size << std::endl;
                success = false;
            } else {
                 std::cout << "preallocateFile Info: Size check for '" << path << "' PASSED. Expected: " << size << ", Actual: " << sb.st_size << std::endl;
            }
        }
    }


    if (::close(fd) != 0 && success) { // If we thought we were successful, but close fails, then we are not.
        std::cerr << "preallocateFile Error: Failed to close file descriptor for '" << path << "': " << strerror(errno) << std::endl;
        success = false; // Mark as failure if close fails
    }

    return success;
}

/**
 * @brief Compute the SHA-256 hash of the provided string.
 *
 * This thin wrapper around libsodium exposes a simple interface for tests
 * that need hashing functionality without requiring direct libsodium calls.
 *
 * @param data Input string to hash.
 * @return Array containing the SHA-256 digest bytes.
 */
inline std::array<unsigned char, crypto_hash_sha256_BYTES>
compute_sha256(const std::string& data) {
    crypto_hash_sha256_state state;
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256_init(&state);
    crypto_hash_sha256_update(&state,
                              reinterpret_cast<const unsigned char*>(data.data()),
                              data.size());
    crypto_hash_sha256_final(&state, digest.data());
    return digest;
}

#endif // SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
