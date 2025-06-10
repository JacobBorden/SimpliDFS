#include "fuse_concurrency_test_utils.hpp"
#include <algorithm> // For std::sort
#include <chrono>    // For timestamps
#include <condition_variable>
#include <cstdio> // For std::remove
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

// Helper for timestamp logging in this specific test file
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

// Configuration
const std::string MOUNT_POINT =
    "/tmp/myfusemount"; // Adjust if your mount point is different
const std::string TEST_FILE_NAME = "concurrent_write_test.txt";
const std::string FULL_TEST_FILE_PATH = MOUNT_POINT + "/" + TEST_FILE_NAME;
const int NUM_THREADS = 5;
const int NUM_LINES_PER_THREAD = 100;
const int LINE_LENGTH = 80; // Length of content part, *before* newline
const std::string HEADER_LINE = "CONCURRENCY_TEST_HEADER_LINE_IGNORE\n";

// Append Test Configuration
const std::string APPEND_TEST_FILE_NAME = "concurrent_append_test.txt";
const std::string FULL_APPEND_TEST_FILE_PATH = MOUNT_POINT + "/" + APPEND_TEST_FILE_NAME;
const int NUM_APPEND_THREADS = 4;
const int NUM_LINES_PER_APPEND_THREAD = 50;
const std::string APPEND_LINE_PREFIX = "AppendThread";
const int APPEND_LINE_FIXED_CONTENT_LENGTH = 60; // For the AAAA... part

// Barrier for synchronizing writer/appender threads
std::mutex start_mutex;
std::condition_variable start_cv;
int start_count = 0;

// Helper function to generate a unique string for each line
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

// Function for each thread to perform writes
void writer_thread_func(int thread_id) {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Starting." << std::endl;

  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count++;
    if (start_count == NUM_THREADS) {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Releasing barrier." << std::endl;
      start_cv.notify_all();
    } else {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Waiting at barrier." << std::endl;
      start_cv.wait(lk, [] { return start_count == NUM_THREADS; });
    }
  }

  std::fstream outfile;

  // Log intention to open the file
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Intending to open file " << FULL_TEST_FILE_PATH
            << std::endl;

  outfile.open(FULL_TEST_FILE_PATH,
               std::ios::out | std::ios::in | std::ios::binary);

  // Log success or failure of open
  if (!outfile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Failed to open file " << FULL_TEST_FILE_PATH
              << ". Stream state: " << outfile.rdstate() << std::endl;
    return;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": File " << FULL_TEST_FILE_PATH
            << " opened successfully." << std::endl;

  for (int i = 0; i < NUM_LINES_PER_THREAD; ++i) {
    std::string content_for_line = generate_line_content(thread_id, i);
    std::string line_to_write = content_for_line + '\n';

    off_t thread_block_start_offset =
        HEADER_LINE.size() +
        thread_id * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);
    off_t line_offset_in_block = i * (LINE_LENGTH + 1);
    off_t offset = thread_block_start_offset + line_offset_in_block;

    // Log target offset before seekp
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Line " << i << ", intending to seek to offset " << offset
              << std::endl;
    outfile.seekp(offset);

    // Log success or failure of seekp
    if (outfile.fail()) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Seekp to " << offset
                << " failed. Stream state: " << outfile.rdstate()
                << ", current position: " << outfile.tellp() << std::endl;
    } else {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Seekp to " << offset << " succeeded. Current position: "
                << outfile.tellp() << std::endl;
    }

    // Log before write: content snippet (or length), intended offset, and number of bytes
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Line " << i << ", intending to write "
              << line_to_write.length() << " bytes at offset " << outfile.tellp()
              << ". Content snippet: " << line_to_write.substr(0, 10) << "..." << std::endl;

    std::streampos pos_before_write = outfile.tellp();
    outfile.write(line_to_write.c_str(), line_to_write.length());

    // Log after write: success/failure, stream state, and bytes written
    if (outfile.fail()) {
      std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Write failed at offset " << pos_before_write
                << ". Stream state: " << outfile.rdstate()
                << ", current position: " << outfile.tellp() << std::endl;
    } else {
      std::streampos pos_after_write = outfile.tellp();
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] Thread "
                << thread_id << ": Write at offset " << pos_before_write
                << " succeeded. Bytes written: " << (pos_after_write - pos_before_write)
                << ". Stream good: " << outfile.good() << std::endl;
    }
  }
  // Log intention to close the file
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Intending to close file " << FULL_TEST_FILE_PATH << std::endl;
  outfile.close();

  // Log whether the close appeared successful
  if (outfile.fail()) { // Check rdstate for errors after close
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": Close operation on file " << FULL_TEST_FILE_PATH
              << " may have failed. Stream state: " << outfile.rdstate() << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] Thread "
              << thread_id << ": File " << FULL_TEST_FILE_PATH << " closed. Stream state good." << std::endl;
  }

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Thread "
            << thread_id << ": Finished." << std::endl;
}

// Function for each thread to perform appends
void appender_thread_func(int thread_id) {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Starting." << std::endl;

  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count++;
    // Use NUM_APPEND_THREADS for this specific barrier condition
    if (start_count == NUM_APPEND_THREADS) {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id << ": Releasing append barrier." << std::endl;
      start_cv.notify_all();
    } else {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id() << "] "
                << APPEND_LINE_PREFIX << " " << thread_id << ": Waiting at append barrier." << std::endl;
      start_cv.wait(lk, [] { return start_count == NUM_APPEND_THREADS; });
    }
  }

  std::fstream outfile;
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Intending to open file "
            << FULL_APPEND_TEST_FILE_PATH << " for append." << std::endl;

  outfile.open(FULL_APPEND_TEST_FILE_PATH,
               std::ios::out | std::ios::app | std::ios::binary);

  if (!outfile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": Failed to open file "
              << FULL_APPEND_TEST_FILE_PATH << " for append. Stream state: " << outfile.rdstate() << std::endl;
    return;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": File "
            << FULL_APPEND_TEST_FILE_PATH << " opened successfully for append." << std::endl;

  for (int i = 0; i < NUM_LINES_PER_APPEND_THREAD; ++i) {
    std::string line_to_write = APPEND_LINE_PREFIX + std::to_string(thread_id) + "_Line" +
                               std::to_string(i) + "_" +
                               std::string(APPEND_LINE_FIXED_CONTENT_LENGTH, 'A' + (thread_id + i) % 26) + "\n";

    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": Line " << i
              << ", intending to append " << line_to_write.length() << " bytes. "
              << "Content snippet: " << line_to_write.substr(0, 20) << "..." << std::endl;

    std::streampos pos_before_write = outfile.tellp();
    outfile.write(line_to_write.c_str(), line_to_write.length());

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
                << APPEND_LINE_PREFIX << " " << thread_id << ": Append succeeded. "
                << "Bytes written: " << (pos_after_write - pos_before_write)
                << ". Stream good: " << outfile.good() << std::endl;
    }
    // Small sleep to increase chance of interleaving
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Intending to close file "
            << FULL_APPEND_TEST_FILE_PATH << std::endl;
  outfile.close();

  if (outfile.fail()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": Close operation on file "
              << FULL_APPEND_TEST_FILE_PATH << " may have failed. Stream state: " << outfile.rdstate() << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id() << "] "
              << APPEND_LINE_PREFIX << " " << thread_id << ": File "
              << FULL_APPEND_TEST_FILE_PATH << " closed. Stream state good." << std::endl;
  }

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] "
            << APPEND_LINE_PREFIX << " " << thread_id << ": Finished." << std::endl;
}


bool check_mount_point_ready() {
  struct stat sb;
  // Add a small delay and extra logging before the first interaction
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Pausing for 1 second before checking mount point..."
            << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Checking mount point " << MOUNT_POINT << " via stat()."
            << std::endl;
  if (stat(MOUNT_POINT.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: stat() successful. Mount point is a directory. "
                 "Checking access..."
              << std::endl;
    if (access(MOUNT_POINT.c_str(), R_OK | W_OK | X_OK) == 0) {
      std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                << " TID: " << std::this_thread::get_id()
                << "] Main: Mount point " << MOUNT_POINT
                << " is ready (stat and access OK)." << std::endl;
      return true;
    }
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Error: Mount point " << MOUNT_POINT
              << " found but not accessible (R/W/X)." << std::endl;
    return false;
  }
  std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Error: Mount point "
            << MOUNT_POINT << " does not exist or is not a directory."
            << std::endl;
  std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Please ensure the FUSE filesystem is mounted and accessible "
               "before running this test."
            << std::endl;
  return false;
}

// Function to verify a single block of lines
bool verify_thread_block(const std::string& payload, int thread_id, size_t& errors_found) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Verifying block for thread " << thread_id << std::endl;
    bool block_ok = true;
    size_t thread_block_start_offset_in_payload = static_cast<size_t>(thread_id) * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);

    for (int line_num = 0; line_num < NUM_LINES_PER_THREAD; ++line_num) {
        std::string expected_line_content = generate_line_content(thread_id, line_num);
        size_t line_start_in_payload = thread_block_start_offset_in_payload + static_cast<size_t>(line_num) * (LINE_LENGTH + 1);

        if (line_start_in_payload + LINE_LENGTH > payload.length()) {
            std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                      << " TID: " << std::this_thread::get_id()
                      << "] BLOCK VERIFICATION ERROR: Thread " << thread_id << ", Line " << line_num
                      << ": Attempting to read beyond payload bounds. "
                      << "Line start: " << line_start_in_payload << ", Expected length: " << LINE_LENGTH
                      << ", Payload length: " << payload.length() << std::endl;
            errors_found++;
            block_ok = false;
            // No point continuing this block if we're out of bounds
            return false;
        }

        std::string actual_line_content = payload.substr(line_start_in_payload, LINE_LENGTH);

        if (actual_line_content != expected_line_content) {
            std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                      << " TID: " << std::this_thread::get_id()
                      << "] BLOCK VERIFICATION ERROR: Thread " << thread_id << ", Line " << line_num
                      << ": Content mismatch." << std::endl;
            std::cerr << "  Expected snippet: " << expected_line_content.substr(0, 20) << "..." << std::endl;
            std::cerr << "  Actual   snippet: " << actual_line_content.substr(0, 20) << "..." << std::endl;
            // For more detailed debugging, one might log the full lines, but snippets are often enough.
            // std::cerr << "  Expected full: " << expected_line_content << std::endl;
            // std::cerr << "  Actual   full: " << actual_line_content << std::endl;
            errors_found++;
            block_ok = false;
        }
    }
    if (block_ok) {
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                  << " TID: " << std::this_thread::get_id()
                  << "] Main: Block for thread " << thread_id << " verified successfully." << std::endl;
    }
    return block_ok;
}

// Function to run the append test
bool run_append_test() {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting." << std::endl;

  if (!check_mount_point_ready()) { // check_mount_point_ready already logs
    return false;
  }

  // Delete the test file if it exists to ensure a clean run
  if (std::remove(FULL_APPEND_TEST_FILE_PATH.c_str()) == 0) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Successfully deleted existing file " << FULL_APPEND_TEST_FILE_PATH << std::endl;
  } else {
    // ENOENT (no such file or directory) is fine. Any other error might be an issue.
    if (errno != ENOENT) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                  << " TID: " << std::this_thread::get_id()
                  << "] AppendTest: Error deleting file " << FULL_APPEND_TEST_FILE_PATH << ": " << strerror(errno)
                  << ". Continuing, but this might indicate a problem." << std::endl;
    } else {
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                  << " TID: " << std::this_thread::get_id()
                  << "] AppendTest: File " << FULL_APPEND_TEST_FILE_PATH << " did not exist, no need to delete." << std::endl;
    }
  }


  // Reset barrier for appender threads
  {
    std::unique_lock<std::mutex> lk(start_mutex);
    start_count = 0;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Barrier count reset for append threads." << std::endl;

  std::vector<std::thread> appender_threads;
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting " << NUM_APPEND_THREADS << " appender threads, each writing "
            << NUM_LINES_PER_APPEND_THREAD << " lines." << std::endl;
  for (int i = 0; i < NUM_APPEND_THREADS; ++i) {
    appender_threads.emplace_back(appender_thread_func, i);
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Appender thread " << i << " created." << std::endl;
  }

  for (auto &t : appender_threads) {
    t.join();
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: All appender threads finished." << std::endl;

  // Verification
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Starting verification phase for " << FULL_APPEND_TEST_FILE_PATH << std::endl;
  std::ifstream infile(FULL_APPEND_TEST_FILE_PATH, std::ios::in | std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION FAILED: Failed to open file for verification: "
              << FULL_APPEND_TEST_FILE_PATH << std::endl;
    return false;
  }

  std::vector<std::string> lines_read;
  std::string current_line;
  while (std::getline(infile, current_line)) {
    lines_read.push_back(current_line);
  }
  infile.close();

  size_t expected_total_append_lines = static_cast<size_t>(NUM_APPEND_THREADS) * NUM_LINES_PER_APPEND_THREAD;
  bool append_test_success = true;

  // Line Count Check
  if (lines_read.size() != expected_total_append_lines) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION FAILED: Line count mismatch. Expected: " << expected_total_append_lines
              << ", Got: " << lines_read.size() << std::endl;
    append_test_success = false;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: Line count matches expected: " << lines_read.size() << std::endl;
  }

  // Content Integrity & Presence Check
  std::vector<std::string> expected_lines;
  for (int t = 0; t < NUM_APPEND_THREADS; ++t) {
    for (int l = 0; l < NUM_LINES_PER_APPEND_THREAD; ++l) {
      // Generate lines without the trailing newline for comparison if getline strips it
      // std::getline typically strips the newline.
      expected_lines.push_back(APPEND_LINE_PREFIX + std::to_string(t) + "_Line" +
                               std::to_string(l) + "_" +
                               std::string(APPEND_LINE_FIXED_CONTENT_LENGTH, 'A' + (t + l) % 26));
    }
  }

  std::sort(lines_read.begin(), lines_read.end());
  std::sort(expected_lines.begin(), expected_lines.end());

  bool content_match = true;
  if (lines_read.size() == expected_lines.size()) { // Only compare if counts were potentially ok or for detailed error
      for (size_t i = 0; i < lines_read.size(); ++i) {
        if (lines_read[i] != expected_lines[i]) {
          std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                    << " TID: " << std::this_thread::get_id()
                    << "] AppendTest: VERIFICATION FAILED: Content mismatch after sort at index " << i << "." << std::endl;
          std::cerr << "  Expected snippet: " << expected_lines[i].substr(0, 40) << "..." << std::endl;
          std::cerr << "  Actual   snippet: " << lines_read[i].substr(0, 40) << "..." << std::endl;
          content_match = false;
          append_test_success = false;
          break;
        }
      }
  } else {
      content_match = false; // Already know it's a failure due to line count
  }


  if (append_test_success && content_match) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION PASSED. All lines accounted for and content matches." << std::endl;
  } else if (append_test_success && !content_match) { // This case implies line count was OK, but content wasn't.
     std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] AppendTest: VERIFICATION FAILED: Content integrity check failed despite correct line count." << std::endl;
     append_test_success = false; // Ensure it's marked as failed
  }
  // If append_test_success is already false due to line count, the FAILED message for line count is primary.

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] AppendTest: Finished. Success: " << (append_test_success ? "Yes" : "No") << std::endl;
  return append_test_success;
}


int main() {
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Test starting." << std::endl;
  if (!check_mount_point_ready()) {
    return 1;
  }

  if (sodium_init() < 0) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Failed to initialize libsodium." << std::endl;
    return 1;
  }

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
    return 1;
  }
  pre_outfile << HEADER_LINE;
  size_t expected_total_lines = NUM_THREADS * NUM_LINES_PER_THREAD;
  const off_t final_size =
      HEADER_LINE.size() +
      static_cast<off_t>(expected_total_lines) * (LINE_LENGTH + 1);
  pre_outfile.close();
  if (!preallocateFile(FULL_TEST_FILE_PATH, final_size)) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Failed to preallocate test file." << std::endl;
    return 1;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id() << "] Main: Test file "
            << TEST_FILE_NAME << " created at " << MOUNT_POINT << std::endl;

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
    t.join();
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: All writer threads finished." << std::endl;

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Starting verification phase." << std::endl;
  std::ifstream infile(FULL_TEST_FILE_PATH, std::ios::in | std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Failed to open file for verification: "
              << FULL_TEST_FILE_PATH << std::endl;
    return 1;
  }

  std::string header_buf(HEADER_LINE.size(), '\0');
  infile.read(header_buf.data(), HEADER_LINE.size());
  bool header_match = (header_buf == HEADER_LINE);
  if (!header_match) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION WARNING: Header line mismatch." << std::endl;
    std::cerr << "Expected: " << HEADER_LINE;
    std::cerr << "Read: " << header_buf << std::endl;
  }

  const size_t remaining_bytes = static_cast<size_t>(NUM_THREADS) *
                                 static_cast<size_t>(NUM_LINES_PER_THREAD) *
                                 (LINE_LENGTH + 1);
  std::string file_payload(remaining_bytes, '\0');
  infile.read(file_payload.data(), remaining_bytes);
  size_t bytes_read = static_cast<size_t>(infile.gcount());
  infile.close(); // Close file as soon as content is read
  if (bytes_read != remaining_bytes) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION WARNING: Expected to read " << remaining_bytes
              << " bytes of payload, but got " << bytes_read << std::endl;
    // If bytes_read is less than expected, file_payload might not be fully populated.
    // The block verification might fail or operate on incomplete data.
    // Consider if this should be a hard failure or if block verification should proceed cautiously.
    // For now, we'll let it proceed, but it's a strong indicator of problems.
    if (bytes_read < remaining_bytes) {
        // Shrink file_payload to actual bytes read to avoid issues with substr later
        // if the read was short. This is a safeguard.
        file_payload.resize(bytes_read);
         std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
                  << " TID: " << std::this_thread::get_id()
                  << "] Payload resized to actual bytes read: " << bytes_read << std::endl;
    }
  }

  // --- Block-Level Verification ---
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Starting block-level data verification." << std::endl;
  bool block_level_verification_failed = false;
  size_t block_verification_errors = 0;

  // Check if payload is too short even before starting loops
  if (file_payload.length() < remaining_bytes && bytes_read == remaining_bytes) {
    // This case implies remaining_bytes was calculated based on full expectation,
    // but file_payload ended up shorter post-read, even if infile.gcount() was initially as expected.
    // This shouldn't happen if infile.read and gcount are consistent.
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] BLOCK VERIFICATION CRITICAL ERROR: file_payload length (" << file_payload.length()
              << ") is less than expected remaining_bytes (" << remaining_bytes
              << ") even though gcount matched. This indicates a deeper issue." << std::endl;
    block_level_verification_failed = true;
  } else {
      for (int t = 0; t < NUM_THREADS; ++t) {
        if (!verify_thread_block(file_payload, t, block_verification_errors)) {
          block_level_verification_failed = true;
          // Decide if you want to stop on first block error or check all blocks
          // For now, let's check all blocks to gather more information.
        }
      }
  }

  if (block_level_verification_failed) {
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Block-level data verification FAILED with " << block_verification_errors << " errors." << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Block-level data verification PASSED." << std::endl;
  }
  // --- End of Block-Level Verification ---

  std::vector<std::string> lines_read_from_file;
  std::stringstream payload_stream(file_payload);
  std::string current_line_read;
  while (std::getline(payload_stream, current_line_read)) {
    lines_read_from_file.push_back(current_line_read);
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: File read for verification. Total lines (excl header): "
            << lines_read_from_file.size() << std::endl;

  bool content_match_success = true;

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

  std::vector<std::string> expected_lines_content;
  for (int i = 0; i < NUM_THREADS; ++i) {
    for (int j = 0; j < NUM_LINES_PER_THREAD; ++j) {
      expected_lines_content.push_back(generate_line_content(i, j));
    }
  }

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
      break;
    }
  }

  if (!content_match_success) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] VERIFICATION FAILED: Data integrity issues detected."
              << std::endl;
  }

  // Compute expected and actual SHA-256 hashes to determine final pass/fail
  std::string expected_combined = HEADER_LINE;
  for (int i = 0; i < NUM_THREADS; ++i) {
    for (int j = 0; j < NUM_LINES_PER_THREAD; ++j) {
      expected_combined += generate_line_content(i, j) + '\n';
    }
  }

  std::string actual_contents = header_buf;
  actual_contents += file_payload; // file_payload might have been resized if read was short

  auto digest_exp = compute_sha256(expected_combined);
  auto digest_act = compute_sha256(actual_contents);

  bool hashes_match =
      std::equal(digest_exp.begin(), digest_exp.end(), digest_act.begin());
  // The sorted content check (`content_match_success`) is still valuable as a different view of integrity.
  // For example, if blocks are correct but some lines are missing (not caught by block check if NUM_LINES_PER_THREAD is off)
  // or if all content is there but in wrong blocks (caught by block check).

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

  bool main_test_success = hashes_match && content_match_success && !block_level_verification_failed;

  if (main_test_success) {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Overall RANDOM WRITE verification PASSED." << std::endl;
  } else {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
              << " TID: " << std::this_thread::get_id()
              << "] Main: Overall RANDOM WRITE verification FAILED." << std::endl;
    if (!hashes_match) std::cerr << "  Reason: Hash mismatch." << std::endl;
    if (!content_match_success) std::cerr << "  Reason: Sorted content mismatch." << std::endl;
    if (block_level_verification_failed) std::cerr << "  Reason: Block-level verification failed." << std::endl;
  }
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Random write test part finished." << std::endl;

  // Run the append test
  std::cout << "======================================================================" << std::endl;
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: Starting APPEND Test Phase." << std::endl;
  std::cout << "======================================================================" << std::endl;
  bool append_test_overall_success = run_append_test();
  std::cout << "======================================================================" << std::endl;
  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: APPEND Test Phase Finished. Success: " << (append_test_overall_success ? "Yes" : "No") << std::endl;
  std::cout << "======================================================================" << std::endl;


  bool all_tests_passed = main_test_success && append_test_overall_success;

  std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp()
            << " TID: " << std::this_thread::get_id()
            << "] Main: All tests finished. Overall result: "
            << (all_tests_passed ? "PASSED" : "FAILED") << std::endl;

  return all_tests_passed ? 0 : 1;
}
