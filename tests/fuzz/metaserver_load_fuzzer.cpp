#include "metaserver/metaserver.h" // Adjust path as necessary
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <fstream> // For std::ofstream
#include <cstdio>  // For std::tmpnam, remove
#include "utilities/logger.h"

// Helper function to write data to a temporary file and return its name
std::string write_to_temp_file(const uint8_t* data, size_t size) {
    // Using a fixed name for simplicity in a hermetic fuzzing environment.
    // In a real scenario, ensure unique names if multiple fuzzers run concurrently.
    std::string temp_filename = "fuzz_temp_metadata.dat";
    std::ofstream ofs(temp_filename, std::ios::binary);
    if (!ofs) {
        // Cannot open file, return empty string to indicate failure.
        // The fuzzer target should handle this gracefully.
        return "";
    }
    ofs.write(reinterpret_cast<const char*>(data), size);
    ofs.close();
    return temp_filename;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    Logger::getInstance().initialize("fuzzer_run.log", LogLevel::ERROR);
    if (Size == 0) {
        return 0;
    }

    MetadataManager mm;

    // Split the input data into two parts, one for each metadata file
    // This is a simple split. More sophisticated strategies could be used.
    size_t split_point = Size / 2;

    std::string file_metadata_content_path;
    std::string node_registry_content_path;

    if (Size > 0) { // Ensure there's data to write
       file_metadata_content_path = write_to_temp_file(Data, split_point);
    } else {
        // Create empty temp files if no data
        file_metadata_content_path = write_to_temp_file(nullptr, 0);
    }

    if (Size > split_point) { // Ensure there's data for the second file
        node_registry_content_path = write_to_temp_file(Data + split_point, Size - split_point);
    } else {
        node_registry_content_path = write_to_temp_file(nullptr, 0);
    }

    // If file creation failed, exit early.
    if (file_metadata_content_path.empty() || node_registry_content_path.empty()) {
        if (!file_metadata_content_path.empty()) std::remove(file_metadata_content_path.c_str());
        if (!node_registry_content_path.empty()) std::remove(node_registry_content_path.c_str());
        return 0;
    }

    try {
        // Call loadMetadata with the paths to the temporary files
        mm.loadMetadata(file_metadata_content_path, node_registry_content_path);

        // Optionally, after loading, call other methods to check consistency or further stress the state
        mm.checkForDeadNodes(); // Example
        mm.printMetadata();     // Example

        // You could also try saving the loaded metadata and comparing it or loading it again.
        std::string temp_save_file_meta = "fuzz_temp_save_meta.dat";
        std::string temp_save_node_reg = "fuzz_temp_save_node_reg.dat";
        mm.saveMetadata(temp_save_file_meta, temp_save_node_reg);

        // Clean up saved files
        std::remove(temp_save_file_meta.c_str());
        std::remove(temp_save_node_reg.c_str());

    } catch (const std::runtime_error& e) {
        // Catch expected exceptions from loadMetadata or other calls
    } catch (const std::invalid_argument& e) {
        // Catch expected exceptions
    }
    // No specific catch for std::out_of_range here unless specific operations are known to throw it directly

    // Clean up the temporary files
    std::remove(file_metadata_content_path.c_str());
    std::remove(node_registry_content_path.c_str());

    return 0;
}
