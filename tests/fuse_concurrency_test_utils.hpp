#pragma once
#ifndef SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
#define SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP

#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <array>
#include <sodium.h>

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
        return false;
    }

    bool success = true;

    if (ftruncate(fd, size) == -1) {
#ifdef __linux__
        if (posix_fallocate(fd, 0, size) != 0) {
            // Fallback to manual extension when posix_fallocate is unsupported
            if (lseek(fd, size - 1, SEEK_SET) == -1 || ::write(fd, "", 1) != 1) {
                success = false;
            }
        }
#else
        if (lseek(fd, size - 1, SEEK_SET) == -1 || ::write(fd, "", 1) != 1) {
            success = false;
        }
#endif
    }

    ::close(fd);
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
