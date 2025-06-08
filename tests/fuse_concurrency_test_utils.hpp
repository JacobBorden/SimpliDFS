#pragma once
#ifndef SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
#define SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP

#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

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
            success = false;
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

#endif // SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
