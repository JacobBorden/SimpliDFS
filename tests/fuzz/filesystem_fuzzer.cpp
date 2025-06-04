#include "utilities/filesystem.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "utilities/logger.h"

// Helper to consume a part of the data as a string.
// Returns a string and advances the data pointer.
std::string consume_string(const uint8_t** data, size_t* size, size_t max_len = 32) {
    if (*size == 0) {
        return "";
    }
    size_t len = 0;
    // Consume a byte for length, scaled to avoid overly long strings
    if (*size > 0) {
        len = (*data)[0] % (max_len + 1); // Max length of max_len
        (*data)++;
        (*size)--;
    }

    if (len > *size) {
        len = *size;
    }

    std::string str(reinterpret_cast<const char*>(*data), len);
    *data += len;
    *size -= len;
    return str;
}

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    Logger::init("fuzzer_run.log", LogLevel::ERROR);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    FileSystem fs;
    const uint8_t* current_data = Data;
    size_t remaining_size = Size;

    // Limit the number of operations to prevent timeouts with large inputs
    // The fuzzer will explore different combinations over multiple runs.
    int operations_limit = 30;

    while (remaining_size > 0 && operations_limit-- > 0) {
        // Consume a byte to decide which operation to perform
        uint8_t operation_choice = 0;
        if (remaining_size > 0) {
            operation_choice = current_data[0];
            current_data++;
            remaining_size--;
        } else {
            break;
        }

        std::string filename1 = consume_string(&current_data, &remaining_size);
        // Ensure filenames are not empty, as this might be invalid for some operations
        if (filename1.empty() && remaining_size > 0) { // If empty and more data, try to make it non-empty
            filename1 = "default_fuzz_file";
        } else if (filename1.empty()){
             // If truly no more data to form a name, maybe skip or use a fixed name
             // For now, operations might fail, which is acceptable for fuzzing
        }


        switch (operation_choice % 7) { // Modulo number of operations
            case 0: { // createFile
                fs.createFile(filename1);
                break;
            }
            case 1: { // writeFile
                std::string content = consume_string(&current_data, &remaining_size, 256); // Larger max_len for content
                fs.writeFile(filename1, content);
                break;
            }
            case 2: { // readFile
                fs.readFile(filename1);
                break;
            }
            case 3: { // deleteFile
                fs.deleteFile(filename1);
                break;
            }
            case 4: { // renameFile
                std::string filename2 = consume_string(&current_data, &remaining_size);
                 if (filename2.empty() && remaining_size > 0) {
                    filename2 = "default_fuzz_file_renamed";
                }
                if (!filename1.empty() && !filename2.empty()) { // Avoid renaming to/from empty if possible
                    fs.renameFile(filename1, filename2);
                }
                break;
            }
            case 5: { // setXattr
                std::string attr_name = consume_string(&current_data, &remaining_size);
                std::string attr_val = consume_string(&current_data, &remaining_size, 64);
                if (!filename1.empty() && !attr_name.empty()){
                     fs.setXattr(filename1, attr_name, attr_val);
                }
                break;
            }
            case 6: { // getXattr
                std::string attr_name = consume_string(&current_data, &remaining_size);
                if (!filename1.empty() && !attr_name.empty()){
                    fs.getXattr(filename1, attr_name);
                }
                break;
            }
        }
    }

    return 0;
}
