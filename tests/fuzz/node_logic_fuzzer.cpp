#include "node/node.h" // Adjust path as necessary
#include "utilities/message.h"
#include "utilities/filesystem.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <stdexcept> // Required for try-catch blocks
#include "utilities/logger.h"

// This fuzzer tests the logic within Node::handleClient by simulating
// the processing of messages and their effects on the internal FileSystem.
// It does not involve actual networking.

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; // Suppress unused parameter warning
    (void)argv; // Suppress unused parameter warning
    Logger::init("fuzzer_run.log", LogLevel::ERROR);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::string input_string(reinterpret_cast<const char*>(Data), Size);
    Message message;

    try {
        message = Message::Deserialize(input_string);
    } catch (const std::runtime_error& e) {
        // Malformed message string, valid fuzzing case
        return 0;
    } catch (const std::invalid_argument& e) {
        // Malformed message string (numeric conversion)
        return 0;
    } catch (const std::out_of_range& e) {
        // Malformed message string (numeric conversion)
        return 0;
    }

    // We need a FileSystem instance, similar to what a Node would have.
    // We don't instantiate a full Node to avoid dealing with its server/networking components directly.
    FileSystem fs;

    // Simulate the core logic of Node::handleClient based on message type
    // This won't cover network send/receive aspects, but will test file operations.
    switch (message._Type) {
        case MessageType::WriteFile: {
            if (message._Content.empty()) { // Treat empty content write as create file
                fs.createFile(message._Filename);
            } else {
                // To be more robust like Node::handleClient, check if file exists first
                // or rely on writeFile's behavior (which is to fail if file doesn't exist).
                // For simplicity, we'll try to create it first, then write.
                // This isn't exactly what Node::handleClient does but tests similar paths.
                fs.createFile(message._Filename); // Ensure it exists
                fs.writeFile(message._Filename, message._Content);
            }
            break;
        }
        case MessageType::ReadFile: {
            fs.readFile(message._Filename);
            break;
        }
        case MessageType::DeleteFile: {
            fs.deleteFile(message._Filename);
            break;
        }
        // ReplicateFileCommand and ReceiveFileCommand in Node::handleClient are mostly stubs
        // or involve complex interactions not easily simulated here without more infrastructure.
        // We are focusing on direct filesystem interactions.
        case MessageType::ReplicateFileCommand:
        case MessageType::ReceiveFileCommand:
            // These primarily involve logging or stubbed network calls in the original Node::handleClient.
            // We can simulate the filesystem part if there was one, e.g., reading a file for replication.
            if (!message._Filename.empty()) {
                 fs.readFile(message._Filename); // Simulate reading the file to be replicated
            }
            break;

        // Other message types from message.h could be added here if they
        // result in direct, testable filesystem operations within Node.
        // For now, focusing on the ones explicitly in Node::handleClient's switch.
        default: {
            // No specific filesystem action for other types in this simplified model.
            break;
        }
    }

    return 0;
}
