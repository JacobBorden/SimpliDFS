#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <numeric>      // For std::iota (not used in this version but good for alternatives)
#include <algorithm>    // For std::sort
#include <sys/stat.h>   // For stat, S_ISDIR
#include <unistd.h>     // For access()
#include <cstdio>       // For std::remove

// Configuration
const std::string MOUNT_POINT = "/tmp/myfusemount"; // Adjust if your mount point is different
const std::string TEST_FILE_NAME = "concurrent_write_test.txt";
const std::string FULL_TEST_FILE_PATH = MOUNT_POINT + "/" + TEST_FILE_NAME;
const int NUM_THREADS = 5;
const int NUM_LINES_PER_THREAD = 100;
// LINE_LENGTH is the length of the content part of the line, *before* the newline character
const int LINE_LENGTH = 80;

// Helper function to generate a unique string for each line of fixed length
std::string generate_line_content(int thread_id, int line_num) {
    std::string line_prefix = "Thread" + std::to_string(thread_id) + "_Line" + std::to_string(line_num) + ": ";
    std::string line_content = line_prefix;
    // Pad with characters to ensure the line (before newline) is exactly LINE_LENGTH characters long
    for (size_t i = 0; line_content.length() < LINE_LENGTH; ++i) {
        line_content += std::to_string((thread_id + line_num + i) % 10);
    }
    // Ensure exact length
    if (line_content.length() > LINE_LENGTH) {
        line_content = line_content.substr(0, LINE_LENGTH);
    } else {
        while (line_content.length() < LINE_LENGTH) {
            line_content += 'X'; // Pad with a consistent character like 'X' or space
        }
    }
    return line_content;
}

// Function for each thread to perform writes
void writer_thread_func(int thread_id) {
    std::fstream outfile;

    // Open for output, binary. File is assumed to be created/truncated by main.
    // std::ios::in is important for fstream if using seekp on a file that is not empty or for read/write.
    outfile.open(FULL_TEST_FILE_PATH, std::ios::out | std::ios::in | std::ios::binary);

    if (!outfile.is_open()) {
        std::cerr << "Thread " << thread_id << ": Failed to open file " << FULL_TEST_FILE_PATH << std::endl;
        return;
    }

    for (int i = 0; i < NUM_LINES_PER_THREAD; ++i) {
        std::string content_for_line = generate_line_content(thread_id, i);
        std::string line_to_write = content_for_line + '\n'; // Add newline character

        // Calculate offset for this line. Each line has length (LINE_LENGTH + 1) including newline.
        // Each thread writes its entire block of lines.
        off_t thread_block_start_offset = thread_id * NUM_LINES_PER_THREAD * (LINE_LENGTH + 1);
        off_t line_offset_in_block = i * (LINE_LENGTH + 1);
        off_t offset = thread_block_start_offset + line_offset_in_block;

        outfile.seekp(offset);
        if (outfile.fail()) {
             std::cerr << "Thread " << thread_id << ": Seekp to " << offset << " failed. Stream state: " << outfile.rdstate() << std::endl;
        }

        outfile.write(line_to_write.c_str(), line_to_write.length());
        if (outfile.fail()) {
            std::cerr << "Thread " << thread_id << ": Write failed at offset " << offset << ". Stream state: " << outfile.rdstate() << std::endl;
        }
        // outfile.flush(); // Optional: Flush frequently.
    }
    outfile.close();
}

bool check_mount_point_ready() {
    struct stat sb;
    if (stat(MOUNT_POINT.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode)) {
        if (access(MOUNT_POINT.c_str(), R_OK | W_OK | X_OK) == 0) {
            return true;
        }
        std::cerr << "Error: Mount point " << MOUNT_POINT << " found but not accessible (R/W/X)." << std::endl;
        return false;
    }
    std::cerr << "Error: Mount point " << MOUNT_POINT << " does not exist or is not a directory." << std::endl;
    std::cerr << "Please ensure the FUSE filesystem is mounted and accessible before running this test." << std::endl;
    return false;
}

int main() {
    if (!check_mount_point_ready()) {
        return 1;
    }

    std::ofstream pre_outfile(FULL_TEST_FILE_PATH, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!pre_outfile.is_open()) {
        std::cerr << "Main: Failed to create/truncate test file: " << FULL_TEST_FILE_PATH << std::endl;
        return 1;
    }
    std::string header_line = "CONCURRENCY_TEST_HEADER_LINE_IGNORE\n"; // Must end with \n

    pre_outfile << header_line;
    pre_outfile.close();
    std::cout << "Main: Test file " << TEST_FILE_NAME << " created/truncated at " << MOUNT_POINT << std::endl;

    std::vector<std::thread> threads_vector;
    std::cout << "Main: Starting " << NUM_THREADS << " writer threads, each writing " << NUM_LINES_PER_THREAD << " lines." << std::endl;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads_vector.emplace_back(writer_thread_func, i);
    }

    for (auto& t : threads_vector) {
        t.join();
    }
    std::cout << "Main: All writer threads finished." << std::endl;

    std::cout << "Main: Starting verification..." << std::endl;
    std::ifstream infile(FULL_TEST_FILE_PATH, std::ios::in | std::ios::binary);
    if (!infile.is_open()) {
        std::cerr << "Main: Failed to open file for verification: " << FULL_TEST_FILE_PATH << std::endl;
        return 1;
    }

    std::vector<std::string> lines_read_from_file;
    std::string current_line_read;

    std::getline(infile, current_line_read); // Read the header line
    // We add '\n' because getline strips it, and header_line includes it.
    if (current_line_read + '\n' != header_line) {
        std::cerr << "VERIFICATION WARNING: Header line mismatch or not found!" << std::endl;
        std::cerr << "Expected header (ending with newline): " << header_line;
        std::cerr << "Read line (getline strips newline): " << current_line_read << std::endl;
    }

    while (std::getline(infile, current_line_read)) {
        lines_read_from_file.push_back(current_line_read);
    }
    infile.close();

    size_t expected_total_lines = NUM_THREADS * NUM_LINES_PER_THREAD;
    if (lines_read_from_file.size() != expected_total_lines) {
        std::cerr << "VERIFICATION FAILED: Line count mismatch." << std::endl;
        std::cerr << "Expected " << expected_total_lines << " lines (excluding header), but read " << lines_read_from_file.size() << " lines." << std::endl;
        return 1;
    }
    std::cout << "Main: Line count matches expected: " << lines_read_from_file.size() << std::endl;

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
            std::cerr << "VERIFICATION FAILED: Content mismatch at sorted line index " << i << std::endl;
            // std::cerr << "Read:     '" << lines_read_from_file[i] << "'" << std::endl; // Can be verbose
            // std::cerr << "Expected: '" << expected_lines_content[i] << "'" << std::endl; // Can be verbose
            content_match_success = false;
            break;
        }
    }

    if (content_match_success) {
        std::cout << "VERIFICATION SUCCESSFUL: All written lines are present and correctly written." << std::endl;
    } else {
        std::cout << "VERIFICATION FAILED: Data integrity issues detected." << std::endl;
        return 1;
    }

    // Optional: Clean up the test file
    // if (std::remove(FULL_TEST_FILE_PATH.c_str()) != 0) {
    //    std::cerr << "Main: Error deleting test file " << FULL_TEST_FILE_PATH << std::endl;
    // } else {
    //    std::cout << "Main: Test file " << FULL_TEST_FILE_PATH << " deleted." << std::endl;
    // }
    return 0;
}
