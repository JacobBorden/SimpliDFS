#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <numeric>      // For std::iota
#include <algorithm>    // For std::sort
#include <sys/stat.h>   // For stat, S_ISDIR
#include <unistd.h>     // For access()
#include <cstdio>       // For std::remove
#include <chrono>       // For timestamps
#include <iomanip>      // For std::put_time
#include <sstream>      // For std::ostringstream
#include <mutex>
#include <condition_variable>

// Helper for timestamp logging in this specific test file
static std::string getFuseTestTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&t); // Ensure this is thread-safe if heavily used in multithreaded logging directly
    oss << std::put_time(&bt, "%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Configuration
const std::string MOUNT_POINT = "/tmp/myfusemount"; // Adjust if your mount point is different
const std::string TEST_FILE_NAME = "concurrent_write_test.txt";
const std::string FULL_TEST_FILE_PATH = MOUNT_POINT + "/" + TEST_FILE_NAME;
const int NUM_THREADS = 5;
const int NUM_LINES_PER_THREAD = 100;
const int LINE_LENGTH = 80; // Length of content part, *before* newline
const std::string HEADER_LINE = "CONCURRENCY_TEST_HEADER_LINE_IGNORE\n";

// Barrier for synchronizing writer threads
std::mutex start_mutex;
std::condition_variable start_cv;
int start_count = 0;

// Helper function to generate a unique string for each line
std::string generate_line_content(int thread_id, int line_num) {
    std::string line_prefix = "Thread" + std::to_string(thread_id) + "_Line" + std::to_string(line_num) + ": ";
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
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Starting." << std::endl;

    {
        std::unique_lock<std::mutex> lk(start_mutex);
        start_count++;
        if (start_count == NUM_THREADS) {
            std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Releasing barrier." << std::endl;
            start_cv.notify_all();
        } else {
            std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Waiting at barrier." << std::endl;
            start_cv.wait(lk, []{ return start_count == NUM_THREADS; });
        }
    }

    std::fstream outfile;

    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Attempting to open file " << FULL_TEST_FILE_PATH << std::endl;
    outfile.open(FULL_TEST_FILE_PATH, std::ios::out | std::ios::in | std::ios::binary);

    if (!outfile.is_open()) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Failed to open file " << FULL_TEST_FILE_PATH << std::endl;
        return;
    }
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": File opened successfully." << std::endl;

    for (int i = 0; i < NUM_LINES_PER_THREAD; ++i) {
        std::string content_for_line = generate_line_content(thread_id, i);
        std::string line_to_write = content_for_line + '\n';

        off_t thread_block_start_offset = HEADER_LINE.size() +
                                         thread_id * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);
        off_t line_offset_in_block = i * (LINE_LENGTH + 1);
        off_t offset = thread_block_start_offset + line_offset_in_block;

        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Line " << i << ", seeking to offset " << offset << std::endl;
        outfile.seekp(offset);
        if (outfile.fail()) {
             std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Seekp to " << offset << " failed. Stream state: " << outfile.rdstate() << std::endl;
        }

        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Line " << i << ", writing " << line_to_write.length() << " bytes." << std::endl;
        outfile.write(line_to_write.c_str(), line_to_write.length());
        if (outfile.fail()) {
            std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Write failed at offset " << offset << ". Stream state: " << outfile.rdstate() << std::endl;
        }
    }
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": All lines written. Closing file." << std::endl;
    outfile.close();
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Thread " << thread_id << ": Finished." << std::endl;
}

bool check_mount_point_ready() {
    struct stat sb;
    // Add a small delay and extra logging before the first interaction
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Pausing for 1 second before checking mount point..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Checking mount point " << MOUNT_POINT << " via stat()." << std::endl;
    if (stat(MOUNT_POINT.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: stat() successful. Mount point is a directory. Checking access..." << std::endl;
        if (access(MOUNT_POINT.c_str(), R_OK | W_OK | X_OK) == 0) {
            std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Mount point " << MOUNT_POINT << " is ready (stat and access OK)." << std::endl;
            return true;
        }
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Error: Mount point " << MOUNT_POINT << " found but not accessible (R/W/X)." << std::endl;
        return false;
    }
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Error: Mount point " << MOUNT_POINT << " does not exist or is not a directory." << std::endl;
    std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Please ensure the FUSE filesystem is mounted and accessible before running this test." << std::endl;
    return false;
}

int main() {
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Test starting." << std::endl;
    if (!check_mount_point_ready()) {
        return 1;
    }

    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Creating/truncating test file " << FULL_TEST_FILE_PATH << std::endl;
    std::fstream pre_outfile(FULL_TEST_FILE_PATH, std::ios::out | std::ios::in | std::ios::trunc | std::ios::binary);
    if (!pre_outfile.is_open()) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Failed to create/truncate test file: " << FULL_TEST_FILE_PATH << std::endl;
        return 1;
    }
    pre_outfile << HEADER_LINE;

    size_t expected_total_lines = NUM_THREADS * NUM_LINES_PER_THREAD;
    size_t final_size = HEADER_LINE.size() + expected_total_lines * (LINE_LENGTH + 1);
    if (final_size > HEADER_LINE.size()) {
        pre_outfile.seekp(final_size - 1);
        pre_outfile.write("\0", 1);
    }
    pre_outfile.close();
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Test file " << TEST_FILE_NAME << " created/truncated and preallocated at " << MOUNT_POINT << std::endl;

    std::vector<std::thread> threads_vector;
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Starting " << NUM_THREADS << " writer threads, each writing " << NUM_LINES_PER_THREAD << " lines." << std::endl;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads_vector.emplace_back(writer_thread_func, i);
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Thread " << i << " created." << std::endl;
    }

    for (auto& t : threads_vector) {
        t.join();
    }
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: All writer threads finished." << std::endl;

    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Starting verification phase." << std::endl;
    std::ifstream infile(FULL_TEST_FILE_PATH, std::ios::in | std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Failed to open file for verification: " << FULL_TEST_FILE_PATH << std::endl;
        return 1;
    }

    std::vector<std::string> lines_read_from_file;
    std::string current_line_read;

    std::getline(infile, current_line_read);
    if (current_line_read + '\n' != HEADER_LINE) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] VERIFICATION WARNING: Header line mismatch or not found!" << std::endl;
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Expected header (ending with newline): " << HEADER_LINE;
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Read line (getline strips newline): " << current_line_read << std::endl;
    }

    while (std::getline(infile, current_line_read)) {
        lines_read_from_file.push_back(current_line_read);
    }
    infile.close();
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: File read for verification. Total lines (excl header): " << lines_read_from_file.size() << std::endl;


    if (lines_read_from_file.size() != expected_total_lines) {
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] VERIFICATION FAILED: Line count mismatch." << std::endl;
        std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Expected " << expected_total_lines << " lines (excluding header), but read " << lines_read_from_file.size() << " lines." << std::endl;
        return 1;
    }
    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Line count matches expected: " << lines_read_from_file.size() << std::endl;

    std::vector<std::string> expected_lines_content;
    for (int i = 0; i < NUM_THREADS; ++i) {
        for (int j = 0; j < NUM_LINES_PER_THREAD; ++j) {
            expected_lines_content.push_back(generate_line_content(i, j));
        }
    }

    std::sort(lines_read_from_file.begin(), lines_read_from_file.end());
    std::sort(expected_lines_content.begin(), expected_lines_content.end());

    bool content_match_success = true;
    for (size_t i = 0; i < lines_read_from_file.size(); ++i) {
        if (lines_read_from_file[i] != expected_lines_content[i]) {
            std::cerr << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] VERIFICATION FAILED: Content mismatch at sorted line index " << i << std::endl;
            content_match_success = false;
            break;
        }
    }

    if (content_match_success) {
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] VERIFICATION SUCCESSFUL: All written lines are present and correctly written." << std::endl;
    } else {
        std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] VERIFICATION FAILED: Data integrity issues detected." << std::endl;
        return 1;
    }

    std::cout << "[FUSE CONCURRENCY LOG " << getFuseTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Main: Test finished." << std::endl;
    return 0;
}
