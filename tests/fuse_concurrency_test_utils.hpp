#pragma once
#ifndef SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
#define SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP

#include <array>
#include <cerrno> // For errno
#include <chrono>
#include <cstring> // For strerror
#include <fcntl.h>
#include <fstream>
#include <iostream> // For std::cerr
#include <sodium.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

/**
 * @brief Preallocate a file to a specified size.
 *
 * This helper function creates or opens the file at the given @p path and
 * attempts to expand it to the target @p size. It's a critical utility for the
 * `FuseConcurrencyTest` suite.
 *
 * For the Random Write Test in `FuseConcurrencyTest`, preallocating the file to
 * its full expected size is essential. It ensures that when multiple threads
 * use `seekp()` to write to their distinct, calculated offsets, they are
 * operating on already allocated disk space. This practice helps prevent race
 * conditions or undefined behavior that might arise if threads were to
 * simultaneously try to extend the file themselves. It is key to the test's
 * design of verifying concurrent writes to non-overlapping, pre-determined
 * regions of a file.
 *
 * The function tries `posix_fallocate` (on Linux) or `ftruncate` first. If
 * these methods are insufficient or fail, it can fall back to a manual method
 * of seeking to the desired size minus one byte and writing a single byte to
 * extend the file. Finally, it performs a size check using `fstat` to confirm
 * the operation.
 *
 * @param path Path to the file to be preallocated.
 * @param size The desired size of the file in bytes after preallocation.
 * @return true on success, false otherwise.
 */
inline bool preallocateFile(const std::string &path, off_t size) {
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    std::cerr << "preallocateFile Error: Failed to open file '" << path
              << "': " << strerror(errno) << std::endl;
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
    std::cerr
        << "preallocateFile Info: posix_fallocate for '" << path << "' to size "
        << size << " failed: "
        << strerror(
               fallocate_ret) // posix_fallocate returns errno value directly
        << ". Attempting ftruncate and manual write." << std::endl;

    if (ftruncate(fd, size) != 0) {
      std::cerr << "preallocateFile Error: ftruncate for '" << path
                << "' to size " << size << " failed: " << strerror(errno)
                << std::endl;
      success = false;
      // Even if ftruncate fails, attempt the manual write as a last resort if
      // size > 0
    }

    // Fallback to manual extension if ftruncate failed or if we want to be
    // absolutely sure (e.g. for older systems or specific FS) This is
    // particularly important if size is 0, as lseek to -1 is invalid.
    if (success && size > 0) { // Only try seek/write if ftruncate seemed to
                               // work and size is positive
      if (lseek(fd, size - 1, SEEK_SET) == -1) {
        std::cerr << "preallocateFile Error: lseek for '" << path
                  << "' to offset " << (size - 1)
                  << " failed: " << strerror(errno) << std::endl;
        success = false;
      } else {
        if (::write(fd, "", 1) != 1) {
          std::cerr << "preallocateFile Error: write of last byte for '" << path
                    << "' at offset " << (size - 1)
                    << " failed: " << strerror(errno) << std::endl;
          success = false;
        }
      }
    } else if (size == 0 && success) {
      // If size is 0, ftruncate should have already set it. No need for
      // seek/write. If ftruncate failed for size 0, success would be false.
    } else if (!success && size > 0) {
      std::cerr << "preallocateFile Info: Skipping manual extension for '"
                << path << "' due to prior ftruncate failure." << std::endl;
    }
  }
#else // Non-Linux systems (e.g., macOS)
  // On non-Linux, try ftruncate first
  if (ftruncate(fd, size) != 0) {
    std::cerr << "preallocateFile Error: ftruncate for '" << path
              << "' to size " << size << " failed: " << strerror(errno)
              << std::endl;
    success = false;
  }

  // Fallback to manual extension if ftruncate failed or if it's the standard
  // way (e.g. on macOS) This is particularly important if size is 0, as lseek
  // to -1 is invalid.
  if (success && size > 0) { // Only try seek/write if ftruncate seemed to work
                             // and size is positive
    if (lseek(fd, size - 1, SEEK_SET) == -1) {
      std::cerr << "preallocateFile Error: lseek for '" << path
                << "' to offset " << (size - 1)
                << " failed: " << strerror(errno) << std::endl;
      success = false;
    } else {
      if (::write(fd, "", 1) != 1) {
        std::cerr << "preallocateFile Error: write of last byte for '" << path
                  << "' at offset " << (size - 1)
                  << " failed: " << strerror(errno) << std::endl;
        success = false;
      }
    }
  } else if (size == 0 && success) {
    // If size is 0, ftruncate should have already set it.
  } else if (!success && size > 0) {
    std::cerr << "preallocateFile Info: Skipping manual extension for '" << path
              << "' due to prior ftruncate failure." << std::endl;
  }
#endif

  // Post-preallocation size check
  if (success) { // Only check size if allocation attempts were thought to be
                 // successful
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
      std::cerr << "preallocateFile Error: fstat failed for file '" << path
                << "': " << strerror(errno) << std::endl;
      success = false;
    } else {
      if (sb.st_size != size) {
        std::cerr << "preallocateFile Error: Size mismatch for file '" << path
                  << "'. Expected: " << size << ", Actual: " << sb.st_size
                  << std::endl;
        success = false;
      } else {
        std::cout << "preallocateFile Info: Size check for '" << path
                  << "' PASSED. Expected: " << size
                  << ", Actual: " << sb.st_size << std::endl;
      }
    }
  }

  if (::close(fd) != 0 && success) { // If we thought we were successful, but
                                     // close fails, then we are not.
    std::cerr << "preallocateFile Error: Failed to close file descriptor for '"
              << path << "': " << strerror(errno) << std::endl;
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
compute_sha256(const std::string &data) {
  crypto_hash_sha256_state state;
  std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
  crypto_hash_sha256_init(&state);
  crypto_hash_sha256_update(
      &state, reinterpret_cast<const unsigned char *>(data.data()),
      data.size());
  crypto_hash_sha256_final(&state, digest.data());
  return digest;
}

/**
 * @brief Attempt to open a file stream with retry logic.
 *
 * This helper is used by the FUSE concurrency tests to tolerate brief
 * delays between when the test threads finish writing and when the
 * filesystem exposes the final file for reading. It repeatedly attempts
 * to open the file before giving up.
 *
 * @param path      Path to the file to open.
 * @param stream    Reference to the stream object to open.
 * @param mode      Open mode flags (e.g., std::ios::in | std::ios::binary).
 * @param retries   Number of additional attempts after the first try.
 * @param delay_ms  Delay in milliseconds between attempts.
 * @return true if the file was successfully opened, false otherwise.
 */
inline bool openFileWithRetry(const std::string &path, std::ifstream &stream,
                              std::ios_base::openmode mode, int retries = 3,
                              int delay_ms = 100) {
  for (int attempt = 0; attempt <= retries; ++attempt) {
    stream.clear();
    stream.open(path, mode);
    if (stream.is_open()) {
      return true;
    }
    stream.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  return false;
}

/**
 * @brief Attempt to open a read/write fstream with retry logic.
 *
 * Some FUSE implementations may briefly delay making newly created
 * files visible to other open calls.  This helper mirrors
 * `openFileWithRetry` but operates on `std::fstream` so writer threads
 * can tolerate that delay when opening the test file for I/O.
 *
 * @param path      Path to the file to open.
 * @param stream    Reference to the stream object to open.
 * @param mode      Open mode flags (e.g. std::ios::in | std::ios::out).
 * @param retries   Number of additional attempts after the first try.
 * @param delay_ms  Delay in milliseconds between attempts.
 * @return true if the file was successfully opened, false otherwise.
 */
inline bool openFstreamWithRetry(const std::string &path, std::fstream &stream,
                                 std::ios_base::openmode mode, int retries = 3,
                                 int delay_ms = 100) {
  for (int attempt = 0; attempt <= retries; ++attempt) {
    stream.clear();
    stream.open(path, mode);
    if (stream.is_open()) {
      return true;
    }
    stream.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  return false;
}

/**
 * @brief Seek the put pointer of an fstream with retry logic.
 *
 * Certain FUSE implementations occasionally return transient errors when
 * repositioning a stream immediately after a write. This helper retries the
 * seek several times, clearing error flags between attempts.
 *
 * @param stream    fstream whose put pointer should be moved.
 * @param offset    Absolute offset from the beginning of the file.
 * @param retries   Number of additional attempts after the first try.
 * @param delay_ms  Delay in milliseconds between attempts.
 * @return true if the seek succeeds, false otherwise.
 */
inline bool seekpWithRetry(std::fstream &stream, std::streamoff offset,
                           int retries = 2, int delay_ms = 50) {
  for (int attempt = 0; attempt <= retries; ++attempt) {
    stream.clear();
    stream.seekp(offset, std::ios::beg);
    if (!stream.fail()) {
      return true;
    }
    stream.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
  }
  return false;
}

#endif // SIMPLIDFS_FUSE_CONCURRENCY_TEST_UTILS_HPP
