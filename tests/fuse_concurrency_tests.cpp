#include "fuse_concurrency_tests.hpp"
#include "fuse_concurrency_test_utils.hpp"
#include <algorithm> // For std::sort
#include <chrono>    // For timestamps
#include <condition_variable>
#include <cstdio>  // For std::remove
#include <cstdlib> // For getenv()
#include <fcntl.h> // For open()
#include <fstream>
#include <iomanip> // For std::put_time
#include <iostream>
#include <mutex>
#include <numeric>  // For std::iota
#include <sodium.h> // For SHA-256 hashing
#include <sstream>  // For std::ostringstream
#include <string>
#include <sys/stat.h> // For stat, S_ISDIR
#include <thread>
#include <unistd.h> // For access()
#include <vector>

// Copyright [Your Copyright]
//
// This file implements a series of concurrency tests for a FUSE filesystem.
// The primary goal is to ensure the filesystem behaves correctly and
// consistently when multiple threads perform simultaneous file operations.
//
// The test suite includes two main scenarios:
// 1. Random Write Test: Multiple threads write to distinct, pre-allocated
//    regions of a single file using explicit offsets (seekp). This tests
//    the filesystem's ability to handle concurrent writes to different parts
//    of a file without data corruption or interference between threads.
// 2. Append Test: Multiple threads concurrently append data to a single file.
//    This tests the atomicity and correctness of append operations under load.
//
// Detailed logging is used throughout the tests to aid in debugging potential
// concurrency issues.

// Helper for timestamp logging in this specific test file
// Returns a formatted timestamp string (HH:MM:SS.mmm) for logging purposes.
// This helps in correlating events from different threads with millisecond
// precision.
static std::string getFuseTestTimestamp() {
  auto now = std::chrono::system_clock::now();
  std::ostringstream oss;
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm bt{};
  localtime_r(&t, &bt); // thread-safe conversion
  oss << std::put_time(&bt, "%H:%M:%S");
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;
  oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
  return oss.str();
}

// Configuration for the Random Write Test
const std::string DEFAULT_MOUNT_POINT =
    "/tmp/myfusemount"; // Fallback path used if env var is unset.

/**
 * @brief Get the mount point for FuseConcurrencyTest.
 *
 * Checks the environment variable "SIMPLIDFS_CONCURRENCY_MOUNT" which is
 * populated by the wrapper script. If defined, its value is returned;
 * otherwise a hard-coded default is used so the test can still run
 * manually.
 *
 * @return Resolved mount point path.
 */
static std::string get_mount_point() {
  const char *env_path = std::getenv("SIMPLIDFS_CONCURRENCY_MOUNT");
  if (env_path && env_path[0] != '\0') {
    return std::string(env_path);
  }
  return DEFAULT_MOUNT_POINT;
}

const std::string MOUNT_POINT = get_mount_point();
const std::string TEST_FILE_NAME =
    "concurrent_write_test.txt"; // Name of the file used for random write
                                 // tests.
const std::string FULL_TEST_FILE_PATH =
    MOUNT_POINT + "/" +
    TEST_FILE_NAME; // Full path to the random write test file.

// Allow the number of worker threads to be overridden at compile time. This
// makes it easy to build alternate test binaries (e.g. single threaded
// versions) by passing a definition such as `-DFUSE_NUM_THREADS=1`.
#ifndef FUSE_NUM_THREADS
#define FUSE_NUM_THREADS 5
#endif
const int NUM_THREADS =
    FUSE_NUM_THREADS; // Number of concurrent threads for the random write test.

const int NUM_LINES_PER_THREAD =
    100; // Number of lines each thread will write in the random write test.
const int LINE_LENGTH =
    80; // Fixed length of the content part of each line, excluding newline.
const std::string HEADER_LINE =
    "CONCURRENCY_TEST_HEADER_LINE_IGNORE\n"; // A header line written once at
                                             // the beginning of the test file.

// Configuration for the Append Test
const std::string APPEND_TEST_FILE_NAME =
    "concurrent_append_test.txt"; // Name of the file used for append tests.
const std::string FULL_APPEND_TEST_FILE_PATH =
    MOUNT_POINT + "/" +
    APPEND_TEST_FILE_NAME; // Full path to the append test file.
// Similar to NUM_THREADS, allow the append test's thread count to be overridden
// when compiling. The default remains four threads.
#ifndef FUSE_NUM_APPEND_THREADS
#define FUSE_NUM_APPEND_THREADS 4
#endif
const int NUM_APPEND_THREADS =
    FUSE_NUM_APPEND_THREADS; // Number of concurrent threads for the append
                             // test.
const int NUM_LINES_PER_APPEND_THREAD =
    50; // Number of lines each thread will append.
const std::string APPEND_LINE_PREFIX =
    "AppendThread"; // Prefix used for lines in the append test to identify the
                    // writing thread.
const int APPEND_LINE_FIXED_CONTENT_LENGTH =
    60; // Fixed length for the content part of lines in the append test (e.g.,
        // "AAAA...").

// Barrier for synchronizing worker threads (both random write and append
// tests). These primitives ensure that all threads are created and ready to
// start their respective file operations (writing or appending) simultaneously.
// This maximizes the chances of race conditions and concurrency issues
// surfacing.
std::mutex start_mutex; // Mutex to protect access to start_count and condition
                        // variable.
std::condition_variable
    start_cv;        // Condition variable to signal threads to start.
int start_count = 0; // Counter for threads that have reached the barrier.

// Helper function to generate a unique and deterministic string for each line
// in the random write test. The content is based on the thread ID and line
// number, ensuring that each line has a predictable payload. This is crucial
// for verifying data integrity after concurrent writes, as the test can check
// if the correct data was written to the correct offset.
std::string generate_line_content(int thread_id, int line_num) {
  std::string line_prefix = "Thread" + std::to_string(thread_id) + "_Line" +
                            std::to_string(line_num) + ": ";
  std::string line_content = line_prefix;
  for (size_t i = 0; line_content.length() < LINE_LENGTH; ++i) {
    line_content += std::to_string((thread_id + line_num + i) % 10);
  }
  if (line_content.length() > LINE_LENGTH) {
    line_content = line_content.substr(0, LINE_LENGTH);
  } else {
    while (line_content.length() < LINE_LENGTH) {
      line_content += 'X';
    }
  }
  return line_content;
}

// Function executed by each thread participating in the Random Write Test.
// Its primary goal is to write a specific number of lines
// (NUM_LINES_PER_THREAD) to pre-determined, distinct offsets within the test
// file (FULL_TEST_FILE_PATH). This tests the FUSE filesystem's ability to
// handle concurrent writes to different parts of a file without data corruption
// or interference between threads.
//
// The extensive std::cout and std::cerr statements throughout this function are
// for detailed logging of thread actions, timings, and potential errors. This
// logging is invaluable for debugging concurrency issues by providing a trace
// of each thread's operations and their outcomes.
void writer_thread_func(int thread_id) {
  // Log thread start
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Starting." << std::endl;

  // Barrier synchronization:
  // This block ensures that all writer threads wait until every thread is ready
  // before any of them start their file operations. This is achieved using a
  // mutex (start_mutex), a condition variable (start_cv), and a counter
  // (start_count). The last thread to increment start_count notifies all other
  // waiting threads.
  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count++;
    if (start_count == NUM_THREADS) {
      // This thread is the last one to reach the barrier, so it notifies
      // others.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Releasing barrier." << std::endl;
      start_cv.notify_all();
    } else {
      // This thread is not the last, so it waits for the signal.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Waiting at barrier." << std::endl;
      start_cv.wait(lk, [] { return start_count == NUM_THREADS; });
    }
  }

  std::fstream outfile; // File stream object for this thread.

  // Log intention to open the file. This helps trace file access patterns.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Intending to open file " << FULL_TEST_FILE_PATH
            << std::endl;

  // Open the test file for writing and reading in binary mode.
  // - std::ios::out: Allows writing to the file.
  // - std::ios::in: Allows reading from the file. This might seem
  // counter-intuitive for a write test,
  //   but fstream often requires it for certain operations like seekp to
  //   arbitrary locations if the file is not truncated, or for checking stream
  //   states reliably. It ensures the stream is fully functional for read/write
  //   operations if needed.
  // - std::ios::binary: Ensures data is written/read without CRLF translation
  // or other text-mode processing.
  outfile.open(FULL_TEST_FILE_PATH,
               std::ios::out | std::ios::in | std::ios::binary);

  // Log success or failure of the open operation. This is critical for
  // diagnosing issues related to file access permissions or mount point
  // problems.
  if (!outfile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Failed to open file " << FULL_TEST_FILE_PATH
              << ". Stream state: " << outfile.rdstate() << std::endl;
    return; // Exit thread if file cannot be opened.
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": File " << FULL_TEST_FILE_PATH
            << " opened successfully." << std::endl;

  // Main loop: Iterates NUM_LINES_PER_THREAD times to write each line assigned
  // to this thread.
  for (int i = 0; i < NUM_LINES_PER_THREAD; ++i) {
    // Generate the unique content for the current line.
    std::string content_for_line = generate_line_content(thread_id, i);
    std::string line_to_write =
        content_for_line + '\n'; // Append newline character.

    // Offset Calculation: This is critical for ensuring threads write to
    // distinct regions. Each thread is assigned a "block" of the file.
    // - HEADER_LINE.size(): Accounts for the initial header line in the file.
    // - thread_id * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1): Calculates the
    // starting
    //   offset for this thread's entire block of lines. Each previous thread
    //   has written NUM_LINES_PER_THREAD lines, each of size (LINE_LENGTH + 1)
    //   bytes (content + newline).
    off_t thread_block_start_offset =
        HEADER_LINE.size() + static_cast<off_t>(thread_id) *
                                 NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);
    // - i * (LINE_LENGTH + 1): Calculates the offset of the current line within
    // this
    //   thread's assigned block.
    off_t line_offset_in_block = static_cast<off_t>(i) * (LINE_LENGTH + 1);
    // - offset: The final, absolute byte offset in the file where this line
    // should be written.
    off_t offset = thread_block_start_offset + line_offset_in_block;

    // Log the target offset before attempting to seek. This helps verify offset
    // calculations.
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Line " << i << ", intending to seek to offset "
              << offset << std::endl;

    // Position the file pointer (put pointer) to the calculated offset.
    // This is key to the "random write" nature of the test, ensuring threads
    // write to their designated, non-overlapping areas, even if the file was
    // pre-allocated.
    bool seek_ok = seekpWithRetry(outfile, offset);

    // Log success or failure of seek, including stream state and current
    // position. This helps debug issues if seek operations are not behaving as
    // expected.
    if (!seek_ok) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Seekp to " << offset
                << " failed. Stream state: " << outfile.rdstate()
                << ", current position: " << outfile.tellp() << std::endl;
      // Depending on strictness, one might choose to break or return here.
    } else {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Seekp to " << offset
                << " succeeded. Current position: " << outfile.tellp()
                << std::endl;
    }

    // Log before writing: content snippet (or length), intended offset, and
    // number of bytes. This provides context for the write operation.
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Line " << i << ", intending to write "
              << line_to_write.length() << " bytes at offset "
              << outfile.tellp()
              << ". Content snippet: " << line_to_write.substr(0, 10) << "..."
              << std::endl;

    std::streampos pos_before_write =
        outfile.tellp(); // Current position before writing.
    // Perform the actual write operation.
    outfile.write(line_to_write.c_str(), line_to_write.length());

    // Log after write: success/failure, stream state, and bytes written.
    // This confirms the outcome of the write and checks for partial writes or
    // errors.
    if (outfile.fail()) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Write failed at offset " << pos_before_write
                << ". Stream state: " << outfile.rdstate()
                << ", current position: " << outfile.tellp() << std::endl;
      // Depending on strictness, one might choose to break or return here.
    } else {
      std::streampos pos_after_write = outfile.tellp();
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Write at offset " << pos_before_write
                << " succeeded. Bytes written: "
                << (pos_after_write - pos_before_write)
                << ". Stream good: " << outfile.good() << std::endl;
    }
  }

  // Log intention to close the file.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Intending to close file " << FULL_TEST_FILE_PATH
            << std::endl;
  // Close the file stream for this thread.
  outfile.close();

  // Log whether the close operation appeared successful by checking stream
  // state. Errors during close can indicate issues with flushing buffers or
  // releasing resources.
  if (outfile.fail()) { // Check rdstate for errors after close
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Close operation on file "
              << FULL_TEST_FILE_PATH
              << " may have failed. Stream state: " << outfile.rdstate()
              << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": File " << FULL_TEST_FILE_PATH
              << " closed. Stream state good." << std::endl;
  }

  // Log thread completion.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Finished." << std::endl;
}

// Function executed by each thread participating in the Append Test.
// Its goal is to concurrently append a specific number of lines
// (NUM_LINES_PER_APPEND_THREAD) to a shared test file
// (FULL_APPEND_TEST_FILE_PATH). This tests the atomicity and correctness of
// append operations under concurrent load.
//
// The std::cout and std::cerr statements provide detailed logging for
// debugging, tracking each thread's progress and any errors encountered during
// append operations.
void appender_thread_func(int thread_id) {
  // Log thread start, including a specific prefix for append threads.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Starting."
            << std::endl;

  // Barrier synchronization:
  // Similar to writer_thread_func, this ensures all appender threads start
  // simultaneously. The start_count is typically reset in run_append_test
  // before launching these threads. This synchronized start is crucial for
  // maximizing contention on the file.
  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count++;
    // Use NUM_APPEND_THREADS for this specific barrier condition.
    if (start_count == NUM_APPEND_THREADS) {
      // Last appender thread to arrive signals others to proceed.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id
                << ": Releasing append barrier." << std::endl;
      start_cv.notify_all();
    } else {
      // Wait for all appender threads to be ready.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id
                << ": Waiting at append barrier." << std::endl;
      start_cv.wait(lk, [] { return start_count == NUM_APPEND_THREADS; });
    }
  }

  std::fstream outfile; // File stream object for this thread.
  // Log intention to open file for append.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id
            << ": Intending to open file " << FULL_APPEND_TEST_FILE_PATH
            << " for append." << std::endl;

  // Open the test file in output, append, and binary modes.
  // - std::ios::out: Allows writing to the file.
  // - std::ios::app: This is CRUCIAL for the append test. It ensures that all
  // write
  //   operations are directed to the current end-of-file, regardless of other
  //   concurrent operations. This mode is key to testing atomic appends.
  // - std::ios::binary: Ensures data is written without text-mode processing.
  outfile.open(FULL_APPEND_TEST_FILE_PATH,
               std::ios::out | std::ios::app | std::ios::binary);

  // Log success or failure of file opening.
  if (!outfile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id
              << ": Failed to open file " << FULL_APPEND_TEST_FILE_PATH
              << " for append. Stream state: " << outfile.rdstate()
              << std::endl;
    return; // Exit thread if file cannot be opened.
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": File "
            << FULL_APPEND_TEST_FILE_PATH << " opened successfully for append."
            << std::endl;

  // Main loop: Iterates NUM_LINES_PER_APPEND_THREAD times to append each line.
  for (int i = 0; i < NUM_LINES_PER_APPEND_THREAD; ++i) {
    // Line Generation: Create a unique line for this thread and iteration.
    // Includes a prefix, thread ID, line number, and a fixed-length content
    // string. This helps in verifying that all lines are written and no data is
    // lost or interleaved incorrectly.
    std::string line_to_write = APPEND_LINE_PREFIX + std::to_string(thread_id) +
                                "_Line" + std::to_string(i) + "_" +
                                std::string(APPEND_LINE_FIXED_CONTENT_LENGTH,
                                            'A' + (thread_id + i) % 26) +
                                "\n";

    // Log before appending: content snippet and length.
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": Line " << i
              << ", intending to append " << line_to_write.length()
              << " bytes. "
              << "Content snippet: " << line_to_write.substr(0, 20) << "..."
              << std::endl;

    std::streampos pos_before_write =
        outfile.tellp(); // Position before append (for logging).
    // Perform the append operation. Due to std::ios::app, the system should
    // ensure this write occurs at the current end-of-file, even with concurrent
    // appends.
    outfile.write(line_to_write.c_str(), line_to_write.length());

    // Log after append: success/failure, stream state, and positions.
    if (outfile.fail()) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id << ": Append failed. "
                << "Stream state: " << outfile.rdstate()
                << ", pos before: " << pos_before_write
                << ", pos after: " << outfile.tellp() << std::endl;
    } else {
      std::streampos pos_after_write = outfile.tellp();
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id
                << ": Append succeeded. "
                << "Bytes written: " << (pos_after_write - pos_before_write)
                << ". Stream good: " << outfile.good() << std::endl;
    }
    // Small sleep to increase chance of thread interleaving:
    // This pause makes it more likely that context switches will occur between
    // threads while they are all trying to append to the file. This helps to
    // expose potential race conditions or atomicity issues in the filesystem's
    // append implementation.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Log intention to close the file.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id
            << ": Intending to close file " << FULL_APPEND_TEST_FILE_PATH
            << std::endl;
  // Close the file stream.
  outfile.close();

  // Log whether the close operation was successful.
  if (outfile.fail()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id
              << ": Close operation on file " << FULL_APPEND_TEST_FILE_PATH
              << " may have failed. Stream state: " << outfile.rdstate()
              << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": File "
              << FULL_APPEND_TEST_FILE_PATH << " closed. Stream state good."
              << std::endl;
  }

  // Log thread completion.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Finished."
            << std::endl;
}

// Verifies that the FUSE filesystem's mount point (MOUNT_POINT) is available
// and operational. This function is a prerequisite for running the actual test
// logic, ensuring that the target filesystem is ready for interaction. It logs
// its actions and any errors encountered, aiding in diagnosing setup issues if
// the tests cannot start. Returns true if the mount point is ready, false
// otherwise.
bool check_mount_point_ready() {
  struct stat
      sb; // Used to store information about the mount point file/directory.

  // Brief pause before the first interaction with the mount point.
  // This can be helpful in scenarios where the FUSE filesystem might be mounted
  // externally and needs a moment to fully initialize. It's a pragmatic
  // approach to potentially improve reliability in some environments.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Pausing for 1 second before checking mount point..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Log the intention to check the mount point using stat().
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Checking mount point " << MOUNT_POINT << " via stat()."
            << std::endl;

  // Use stat() to get information about the mount point.
  // We check if stat() succeeds (returns 0) and if the entry is a directory
  // (S_ISDIR).
  if (stat(MOUNT_POINT.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
    // Log success of stat() and that it's a directory.
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: stat() successful. Mount point is a directory. "
                 "Checking access..."
              << std::endl;

    // Use access() to check for read (R_OK), write (W_OK), and execute (X_OK)
    // permissions. For a FUSE mount point to be usable by the tests, these
    // permissions are typically needed.
    if (access(MOUNT_POINT.c_str(), R_OK | W_OK | X_OK) == 0) {
      // Log that all checks passed and the mount point is ready.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] Main: Mount point " << MOUNT_POINT
                << " is ready (stat and access OK)." << std::endl;
      return true; // Mount point is ready.
    }
    // Log an error if access() fails, indicating a permission issue.
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Error: Mount point " << MOUNT_POINT
              << " found but not accessible (R/W/X)." << std::endl;
    return false; // Mount point exists but is not accessible with required
                  // permissions.
  }
  // Log an error if stat() fails or if the mount point is not a directory.
  std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Error: Mount point "
            << MOUNT_POINT << " does not exist or is not a directory."
            << std::endl;
  std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Please ensure the FUSE filesystem is mounted and accessible "
               "before running this test."
            << std::endl;
  return false; // Mount point is not ready (doesn't exist or isn't a
                // directory).
}

// Verifies the integrity of data written by a specific thread within its
// designated block in the file. This function is a crucial part of the Random
// Write Test's verification phase. It checks if the content written by a single
// thread to its allocated region matches the expected content.
//
// Parameters:
//   payload: A string_view or const std::string& representing the portion of
//   the file content
//            that contains the blocks written by all threads (typically, this
//            excludes the global file header).
//   thread_id: The ID of the thread whose written block is to be verified. This
//   determines
//              which specific segment of the payload and what expected content
//              to check.
//   errors_found: A reference to a size_t counter that is incremented if any
//   discrepancies
//                 are found. This allows the caller to accumulate a total error
//                 count.
//
// Logic:
//   1. Calculates the expected starting offset of the specified thread's block
//   within the payload.
//   2. Iterates through each line that the thread was supposed to write.
//   3. For each line, it generates the expected content using
//   `generate_line_content()`.
//   4. It then compares this expected content with the actual content extracted
//   from the
//      `payload` at the calculated position for that line.
//   5. Logs detailed error messages if mismatches or out-of-bounds reads are
//   detected.
//
// Returns:
//   true if the entire block for the specified thread_id is correct (all lines
//   match expected content). false if any mismatch is found or if there's an
//   issue accessing the payload (e.g., out of bounds).
bool verify_thread_block(const std::string &payload, int thread_id,
                         size_t &errors_found) {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Verifying block for thread " << thread_id << std::endl;
  bool block_ok = true;
  size_t thread_block_start_offset_in_payload =
      static_cast<size_t>(thread_id) * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);

  for (int line_num = 0; line_num < NUM_LINES_PER_THREAD; ++line_num) {
    std::string expected_line_content =
        generate_line_content(thread_id, line_num);
    size_t line_start_in_payload =
        thread_block_start_offset_in_payload +
        static_cast<size_t>(line_num) * (LINE_LENGTH + 1);

    if (line_start_in_payload + LINE_LENGTH > payload.length()) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] BLOCK VERIFICATION ERROR: Thread " << thread_id
                << ", Line " << line_num
                << ": Attempting to read beyond payload bounds. "
                << "Line start: " << line_start_in_payload
                << ", Expected length: " << LINE_LENGTH
                << ", Payload length: " << payload.length() << std::endl;
      errors_found++;
      block_ok = false;
      // No point continuing this block if we're out of bounds
      return false;
    }

    std::string actual_line_content =
        payload.substr(line_start_in_payload, LINE_LENGTH);

    if (actual_line_content != expected_line_content) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] BLOCK VERIFICATION ERROR: Thread " << thread_id
                << ", Line " << line_num << ": Content mismatch." << std::endl;
      std::cerr << "  Expected snippet: " << expected_line_content.substr(0, 20)
                << "..." << std::endl;
      std::cerr << "  Actual   snippet: " << actual_line_content.substr(0, 20)
                << "..." << std::endl;
      // For more detailed debugging, one might log the full lines, but snippets
      // are often enough. std::cerr << "  Expected full: " <<
      // expected_line_content << std::endl; std::cerr << "  Actual   full: " <<
      // actual_line_content << std::endl;
      errors_found++;
      block_ok = false;
    }
  }
  if (block_ok) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Block for thread " << thread_id
              << " verified successfully." << std::endl;
  }
  return block_ok;
}

// Encapsulates the entire Append Test, verifying the correctness and atomicity
// of concurrent append operations to a single file.
//
// The test proceeds in several stages:
// 1. Setup: Ensures the mount point is ready and cleans up any previous test
// file.
//    Resets the barrier counter for appender threads.
// 2. Thread Launching: Creates and starts multiple `appender_thread_func`
// instances,
//    each of which will attempt to append lines to the shared file.
// 3. Execution: Waits for all appender threads to complete.
// 4. Verification: Reads the content of the append test file and performs
// checks:
//    a. Line Count Check: Verifies if the total number of lines in the file
//    matches
//       the total number of lines all threads were supposed to write.
//    b. Content Integrity & Presence Check: Verifies that every expected line
//       is present in the file and has the correct content. This is done by
//       sorting both the read lines and a generated list of all expected lines,
//       then comparing them. Sorting is necessary because concurrent appends
//       (using std::ios::app) guarantee atomic writes but not a specific order
//       of lines from different threads.
//
// Returns:
//   true if all verification steps (correct line count and all lines present
//   with correct content) pass, indicating successful and atomic concurrent
//   appends. false otherwise.
bool run_append_test() {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting." << std::endl;

  // --- Initial Setup for Append Test ---
  // Ensure the FUSE mount point is accessible before proceeding.
  if (!check_mount_point_ready()) { // check_mount_point_ready already logs
                                    // errors
    return false; // Cannot run test if mount point is not ready.
  }

  // Delete the append test file if it exists from a previous run to ensure a
  // clean state.
  if (std::remove(FULL_APPEND_TEST_FILE_PATH.c_str()) == 0) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Successfully deleted existing file "
              << FULL_APPEND_TEST_FILE_PATH << std::endl;
  } else {
    // If std::remove fails, check errno. ENOENT (No such file or directory) is
    // acceptable, as it means the file didn't exist, which is a clean state.
    // Other errors might be problematic.
    if (errno != ENOENT) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] AppendTest: Error deleting file "
                << FULL_APPEND_TEST_FILE_PATH << ": " << strerror(errno)
                << ". Continuing, but this might indicate a problem."
                << std::endl;
    } else {
      // Log that the file didn't exist, so no deletion was needed.
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] AppendTest: File " << FULL_APPEND_TEST_FILE_PATH
                << " did not exist, no need to delete." << std::endl;
    }
  }

  // Reset the barrier counter (start_count) specifically for the appender
  // threads. This ensures the appender threads synchronize their start
  // independently of the random writer threads if they used the same barrier
  // variables.
  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count = 0;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Barrier count reset for append threads."
            << std::endl;

  // --- Thread Launching ---
  std::vector<std::thread>
      appender_threads; // Vector to hold appender thread objects.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting " << NUM_APPEND_THREADS
            << " appender threads, each writing " << NUM_LINES_PER_APPEND_THREAD
            << " lines." << std::endl;
  // Create and launch each appender thread. Each thread will execute
  // appender_thread_func.
  for (int i = 0; i < NUM_APPEND_THREADS; ++i) {
    appender_threads.emplace_back(appender_thread_func, i);
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Appender thread " << i << " created."
              << std::endl;
  }

  // Wait for all appender threads to complete their execution.
  // This ensures all append operations are finished before verification begins.
  for (auto &t : appender_threads) {
    t.join();
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: All appender threads finished." << std::endl;

  int fd_sync = ::open(FULL_APPEND_TEST_FILE_PATH.c_str(), O_WRONLY);
  if (fd_sync >= 0) {
    ::fsync(fd_sync);
    ::close(fd_sync);
  }

  // --- Verification Phase ---
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting verification phase for "
            << FULL_APPEND_TEST_FILE_PATH << std::endl;
  // Open the append test file for reading. Similar to the random write test we
  // retry a few times to allow the filesystem to settle.
  std::ifstream infile;
  if (!openFileWithRetry(FULL_APPEND_TEST_FILE_PATH, infile,
                         std::ios::in | std::ios::binary, 5, 200)) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION FAILED: Failed to open file for "
                 "verification: "
              << FULL_APPEND_TEST_FILE_PATH << std::endl;
    return false; // Cannot verify if the file cannot be opened.
  }

  // Read all lines from the append test file into a vector of strings.
  std::vector<std::string> lines_read;
  std::string current_line;
  while (std::getline(infile, current_line)) {
    lines_read.push_back(current_line);
  }
  infile.close(); // Close the file after reading.

  // Calculate the total number of lines expected to be in the file.
  size_t expected_total_append_lines =
      static_cast<size_t>(NUM_APPEND_THREADS) * NUM_LINES_PER_APPEND_THREAD;
  bool append_test_success = true; // Assume success initially.

  // Line Count Check: Verify if the number of lines read matches the expected
  // total. This is a basic check for data completeness (no lost lines).
  if (lines_read.size() != expected_total_append_lines) {
    std::cerr
        << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
        << " TID: " << std::this_thread::get_id()
        << "] AppendTest: VERIFICATION FAILED: Line count mismatch. Expected: "
        << expected_total_append_lines << ", Got: " << lines_read.size()
        << std::endl;
    append_test_success = false; // Mark test as failed if line counts differ.
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Line count matches expected: "
              << lines_read.size() << std::endl;
  }

  // Content Integrity & Presence Check (Sorted Verification):
  // Generate a list of all lines that all threads were supposed to write.
  std::vector<std::string> expected_lines;
  for (int t = 0; t < NUM_APPEND_THREADS; ++t) {
    for (int l = 0; l < NUM_LINES_PER_APPEND_THREAD; ++l) {
      // Note: std::getline strips the newline, so expected lines are generated
      // without it for comparison.
      expected_lines.push_back(
          APPEND_LINE_PREFIX + std::to_string(t) + "_Line" + std::to_string(l) +
          "_" +
          std::string(APPEND_LINE_FIXED_CONTENT_LENGTH, 'A' + (t + l) % 26));
    }
  }

  // Sort both the lines read from the file and the generated expected lines.
  // Why sorting? Concurrent appends (std::ios::app) ensure each write is atomic
  // (goes to the end), but the order in which lines from different threads
  // appear in the file is non-deterministic. Sorting allows us to verify that
  // *all* expected data is present and correct, regardless of the interleaved
  // order resulting from concurrent execution.
  std::sort(lines_read.begin(), lines_read.end());
  std::sort(expected_lines.begin(), expected_lines.end());

  bool content_match = true; // Assume content matches initially.
  // Only compare content if line counts were potentially okay or for detailed
  // error reporting.
  if (lines_read.size() == expected_lines.size()) {
    for (size_t i = 0; i < lines_read.size(); ++i) {
      if (lines_read[i] != expected_lines[i]) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                  << " TID: " << std::this_thread::get_id()
                  << "] AppendTest: VERIFICATION FAILED: Content mismatch "
                     "after sort at index "
                  << i << "." << std::endl;
        std::cerr << "  Expected snippet: " << expected_lines[i].substr(0, 40)
                  << "..." << std::endl;
        std::cerr << "  Actual   snippet: " << lines_read[i].substr(0, 40)
                  << "..." << std::endl;
        content_match = false;
        append_test_success = false; // Mark test as failed on content mismatch.
        break;                       // Stop on first mismatch.
      }
    }
  } else {
    content_match = false; // Content cannot match if line counts are different.
    // append_test_success would already be false from the line count check.
  }

  // Final determination of append test success.
  // The test passes if and only if:
  // 1. The line count check passed (`append_test_success` is still true from
  // that).
  // 2. The sorted content check passed (`content_match` is true).
  if (append_test_success && content_match) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION PASSED. All lines accounted for "
                 "and content matches."
              << std::endl;
  } else if (append_test_success && !content_match) {
    // This case implies line count was OK, but content mismatch was found.
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION FAILED: Content integrity check "
                 "failed despite correct line count."
              << std::endl;
    append_test_success = false; // Ensure it's explicitly marked as failed.
  }
  // If append_test_success was already false due to line count, that error
  // message is primary.

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Finished. Success: "
            << (append_test_success ? "Yes" : "No") << std::endl;
  // Return the overall success status of the append test.
  return append_test_success;
}

bool run_random_write_test() {
  // Main function: Entry point for the FUSE concurrency test program.
  // Orchestrates the execution of different test scenarios (Random Write,
  // Append).
  // --- Initial Test Setup ---
  // Log the start of the entire test suite.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Test starting." << std::endl;

  // Verify the FUSE mount point is ready before proceeding.
  if (!check_mount_point_ready()) {
    // check_mount_point_ready() already logs the error details.
    return 1; // Exit if mount point isn't ready.
  }

  // Initialize the libsodium library, which is used for SHA-256 hashing during
  // verification.
  if (sodium_init() < 0) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Failed to initialize libsodium." << std::endl;
    return 1; // Exit if libsodium cannot be initialized.
  }

  // Create or truncate the main test file for the Random Write Test.
  // This ensures a clean state for each test run.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Creating/truncating test file " << FULL_TEST_FILE_PATH
            << std::endl;
  std::ofstream pre_outfile(FULL_TEST_FILE_PATH,
                            std::ios::out | std::ios::trunc | std::ios::binary);
  if (!pre_outfile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Failed to create/truncate test file: "
              << FULL_TEST_FILE_PATH << std::endl;
    return 1; // Exit if file creation/truncation fails.
  }
  // Write a header line to the test file. This helps in identifying the file
  // and also serves as a non-payload part for offset calculations.
  pre_outfile << HEADER_LINE;

  // Calculate the total expected size of the file after all threads have
  // written their content. This includes the header and all lines from all
  // threads.
  size_t expected_total_lines = NUM_THREADS * NUM_LINES_PER_THREAD;
  const off_t final_size =
      HEADER_LINE.size() +
      static_cast<off_t>(expected_total_lines) *
          (LINE_LENGTH + 1); // (LINE_LENGTH + 1 for newline)
  pre_outfile.close();       // Close the file after writing the header.

  // Preallocate the test file to its full expected size.
  // This is a CRUCIAL step for the Random Write Test.
  // Why preallocation is important:
  // 1. Simulates writing to pre-defined, fixed regions: The test aims to verify
  // that threads
  //    can write to their specific byte offsets (`seekp()`) within a file
  //    without interference. Preallocating ensures these regions physically
  //    exist (or are at least accounted for by the FS) before threads attempt
  //    to write to them.
  // 2. Avoids file extension races: If the file were not preallocated, multiple
  // threads
  //    writing with `seekp()` beyond the current end-of-file might trigger
  //    concurrent file extension operations. This could lead to race
  //    conditions, non-deterministic behavior, or performance anomalies that
  //    are not the primary focus of testing `seekp()` to *known* locations.
  // 3. Ensures distinct blocks: Preallocation helps solidify the concept of
  // each thread "owning"
  //    a distinct block of the file space for its writes.
  if (!preallocateFile(FULL_TEST_FILE_PATH, final_size)) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Failed to preallocate test file." << std::endl;
    return 1; // Exit if preallocation fails.
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Main: Test file "
            << TEST_FILE_NAME << " created at " << MOUNT_POINT << std::endl;

  // --- Start of Random Write Test Execution ---
  std::vector<std::thread> threads_vector;
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Main: Starting "
            << NUM_THREADS << " writer threads, each writing "
            << NUM_LINES_PER_THREAD << " lines." << std::endl;
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads_vector.emplace_back(writer_thread_func, i);
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Main: Thread "
              << i << " created." << std::endl;
  }

  for (auto &t : threads_vector) {
    t.join(); // Wait for all writer threads to complete their execution.
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: All writer threads finished." << std::endl;

  // --- Verification Phase for Random Write Test ---
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Starting verification phase." << std::endl;

  // Open the test file for reading its contents for verification. The FUSE
  // layer may take a short moment to expose the file after all writes
  // complete, so retry a few times before giving up.
  std::ifstream infile;
  if (!openFileWithRetry(FULL_TEST_FILE_PATH, infile,
                         std::ios::in | std::ios::binary, 5, 200)) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Failed to open file for verification after retries: "
              << FULL_TEST_FILE_PATH << std::endl;
    return 1; // Cannot proceed with verification if the file can't be opened.
  }

  // Read and verify the header line.
  std::string header_buf(HEADER_LINE.size(), '\0');
  infile.read(header_buf.data(), HEADER_LINE.size());
  bool header_match = (header_buf == HEADER_LINE);
  if (!header_match) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION WARNING: Header line mismatch." << std::endl;
    std::cerr << "Expected: " << HEADER_LINE;
    std::cerr << "Read: " << header_buf << std::endl;
    // This is a warning; verification can continue, but it indicates a problem.
  }

  // Calculate the expected size of the payload (all lines written by all
  // threads).
  const size_t remaining_bytes = static_cast<size_t>(NUM_THREADS) *
                                 static_cast<size_t>(NUM_LINES_PER_THREAD) *
                                 (LINE_LENGTH + 1); // +1 for newline
  // Read the entire payload into a string.
  std::string file_payload(remaining_bytes, '\0');
  infile.read(file_payload.data(), remaining_bytes);
  size_t bytes_read =
      static_cast<size_t>(infile.gcount()); // Get actual number of bytes read.
  infile.close(); // Close the file as soon as its content is read.

  // Check if the number of bytes read matches the expected payload size.
  if (bytes_read != remaining_bytes) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION WARNING: Expected to read " << remaining_bytes
              << " bytes of payload, but got " << bytes_read << std::endl;
    // If fewer bytes were read than expected, it's a strong indicator of data
    // loss or truncation. The block verification below might operate on
    // incomplete data or fail.
    if (bytes_read < remaining_bytes) {
      // Safeguard: Resize file_payload to actual bytes read to prevent substr
      // from going out of bounds in verify_thread_block if the read was short.
      file_payload.resize(bytes_read);
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] Payload resized to actual bytes read: " << bytes_read
                << std::endl;
    }
  }

  // --- Block-Level Verification ---
  // This step checks if each thread wrote its data into its correct, designated
  // region of the file.
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Starting block-level data verification." << std::endl;
  bool block_level_verification_failed =
      false; // Flag to track if any block is incorrect.
  size_t block_verification_errors =
      0; // Counter for errors found by verify_thread_block.

  // Defensive check: If the payload length is unexpectedly short even if gcount
  // initially matched, it signals a critical issue. This should ideally not
  // happen if infile.read and gcount are consistent.
  if (file_payload.length() < remaining_bytes &&
      bytes_read == remaining_bytes) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] BLOCK VERIFICATION CRITICAL ERROR: file_payload length ("
              << file_payload.length()
              << ") is less than expected remaining_bytes (" << remaining_bytes
              << ") even though gcount matched. This indicates a deeper issue."
              << std::endl;
    block_level_verification_failed = true;
  } else {
    // Iterate through each thread and verify its corresponding block in the
    // payload.
    for (int t = 0; t < NUM_THREADS; ++t) {
      if (!verify_thread_block(file_payload, t, block_verification_errors)) {
        block_level_verification_failed = true;
        // Continue checking all blocks to gather comprehensive error
        // information, rather than stopping at the first block error.
      }
    }
  }

  if (block_level_verification_failed) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Block-level data verification FAILED with "
              << block_verification_errors << " errors." << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Block-level data verification PASSED." << std::endl;
  }
  // --- End of Block-Level Verification ---

  // --- Sorted Content Verification (Line-by-Line) ---
  // This step checks if all expected lines of content are present *somewhere*
  // in the file payload and correctly formatted, irrespective of their exact
  // position (which block-level verification covers). It helps distinguish data
  // loss/corruption from data misplacement.
  std::vector<std::string> lines_read_from_file;
  std::stringstream payload_stream(
      file_payload); // Use a stringstream to parse lines from the payload.
  std::string current_line_read;
  while (std::getline(payload_stream, current_line_read)) {
    lines_read_from_file.push_back(current_line_read);
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: File read for verification. Total lines (excl header): "
            << lines_read_from_file.size() << std::endl;

  bool content_match_success = true; // Flag for sorted content match.

  // Check if the total number of lines read matches the expected number.
  if (lines_read_from_file.size() != expected_total_lines) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION WARNING: Line count mismatch." << std::endl;
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Expected "
              << expected_total_lines << " lines (excluding header), but read "
              << lines_read_from_file.size() << " lines." << std::endl;
    content_match_success = false;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Line count matches expected: "
              << lines_read_from_file.size() << std::endl;
  }

  // Generate all expected lines of content.
  std::vector<std::string> expected_lines_content;
  for (int i = 0; i < NUM_THREADS; ++i) {
    for (int j = 0; j < NUM_LINES_PER_THREAD; ++j) {
      expected_lines_content.push_back(generate_line_content(i, j));
    }
  }

  // Sort both the read lines and expected lines, then compare them element by
  // element. This verifies that all data is present and correct, even if it got
  // shuffled (which block-level verification would have caught as an error).
  std::sort(lines_read_from_file.begin(), lines_read_from_file.end());
  std::sort(expected_lines_content.begin(), expected_lines_content.end());
  for (size_t i = 0; i < lines_read_from_file.size(); ++i) {
    if (lines_read_from_file[i] != expected_lines_content[i]) {
      std::cerr
          << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
          << " TID: " << std::this_thread::get_id()
          << "] VERIFICATION FAILED: Content mismatch at sorted line index "
          << i << std::endl;
      content_match_success = false;
      break; // Stop on first mismatch.
    }
  }

  if (!content_match_success) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION FAILED: Data integrity issues detected "
                 "(sorted content mismatch)."
              << std::endl;
  }
  // --- End of Sorted Content Verification ---

  // --- SHA-256 Hash Verification ---
  // This is the most stringent and primary verification for the random write
  // test. It constructs the expected full file content (header + all lines in
  // their original, intended order) and compares its SHA-256 hash with the hash
  // of the actual file content read from disk. A matching hash indicates that
  // the file on disk is byte-for-byte identical to the perfectly constructed
  // expected file, implying all concurrent writes were successful, complete,
  // and non-interfering at their designated pre-allocated offsets.

  // Construct the complete expected file content in memory.
  std::string expected_combined = HEADER_LINE;
  for (int i = 0; i < NUM_THREADS; ++i) {
    for (int j = 0; j < NUM_LINES_PER_THREAD; ++j) {
      expected_combined += generate_line_content(i, j) + '\n';
    }
  }

  // Construct the actual file content as read from disk (header + payload).
  // Note: file_payload might have been resized if the initial read was short.
  std::string actual_contents = header_buf;
  actual_contents += file_payload;

  // Compute SHA-256 hashes for both expected and actual contents.
  auto digest_exp = compute_sha256(expected_combined);
  auto digest_act = compute_sha256(actual_contents);

  // Compare the hashes.
  bool hashes_match =
      std::equal(digest_exp.begin(), digest_exp.end(), digest_act.begin());

  // Log hash comparison results.
  std::ostringstream exp_hex;
  exp_hex << std::hex << std::setfill('0');
  std::ostringstream act_hex;
  act_hex << std::hex << std::setfill('0');
  for (unsigned char c : digest_exp)
    exp_hex << std::setw(2) << static_cast<int>(c);
  for (unsigned char c : digest_act)
    act_hex << std::setw(2) << static_cast<int>(c);

  if (hashes_match) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Hash verification successful." << std::endl;
  } else {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Hash mismatch!"
              << std::endl;
    std::cerr << "Expected SHA256: " << exp_hex.str() << std::endl;
    std::cerr << "Actual   SHA256: " << act_hex.str() << std::endl;
  }
  // --- End of SHA-256 Hash Verification ---

  // Determine overall success of the Random Write Test.
  // All three conditions must be true for the test to be considered fully
  // passed:
  // 1. `hashes_match`: Confirms overall file integrity at the byte level. If
  // this is true,
  //    the other two should generally also be true. It's the most comprehensive
  //    check.
  // 2. `content_match_success`: Confirms all expected content lines are present
  // and correct
  //    (after sorting), guarding against data loss or corruption.
  // 3. `!block_level_verification_failed`: Confirms each thread's data is in
  // its designated block,
  //    guarding against data misplacement.
  // While `hashes_match` implies the others, having separate checks provides
  // more granular debugging information in case of failures. For example, if
  // hashes don't match, the block-level or sorted-content checks can help
  // pinpoint if it's due to misplacement, corruption, or missing data.
  bool main_test_success =
      hashes_match && content_match_success && !block_level_verification_failed;

  if (main_test_success) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Overall RANDOM WRITE verification PASSED."
              << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Overall RANDOM WRITE verification FAILED."
              << std::endl;
    // Provide reasons for failure based on the flags.
    if (!hashes_match)
      std::cerr << "  Reason: Hash mismatch." << std::endl;
    if (!content_match_success)
      std::cerr << "  Reason: Sorted content mismatch." << std::endl;
    if (block_level_verification_failed)
      std::cerr << "  Reason: Block-level verification failed." << std::endl;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Random write test part finished." << std::endl;

  bool overall_success = main_test_success;

  // --- Overlapping Extent Test ---
  const std::string overlap_file = FULL_TEST_FILE_PATH + ".overlap";
  {
    std::ofstream pre(overlap_file, std::ios::binary | std::ios::trunc);
    pre.seekp(4096 - 1);
    pre.write("", 1);
  }

  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count = 0;
  }

  auto overlap_worker = [&](int tid) {
    std::fstream f(overlap_file,
                   std::ios::in | std::ios::out | std::ios::binary);
    {
      std::unique_lock<std::mutex> lk(start_mutex);
      start_count++;
      if (start_count == NUM_THREADS)
        start_cv.notify_all();
      else
        start_cv.wait(lk, [] { return start_count == NUM_THREADS; });
    }
    off_t off = tid * 8;
    std::string data = "block" + std::to_string(tid);
    f.seekp(off);
    f.write(data.c_str(), data.size());
    f.close();
  };

  std::vector<std::thread> overlap_threads;
  for (int i = 0; i < NUM_THREADS; ++i)
    overlap_threads.emplace_back(overlap_worker, i);
  for (auto &t : overlap_threads)
    t.join();

  std::ifstream verify(overlap_file, std::ios::binary);
  std::string contents((std::istreambuf_iterator<char>(verify)),
                       std::istreambuf_iterator<char>());
  verify.close();

  bool overlap_ok = true;
  for (int i = 0; i < NUM_THREADS; ++i) {
    std::string expected = "block" + std::to_string(i);
    if (contents.substr(i * 8, expected.size()) != expected) {
      overlap_ok = false;
      break;
    }
  }

  overall_success = overall_success && overlap_ok;

  return overall_success;
}
