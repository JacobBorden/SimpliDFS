// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "filesystem.h" // Assuming filesystem.h provides necessary interfaces from filesystem.cpp
#include "message.h" // Including message for node communication
#include <sstream>

// Metadata class to manage file locations and details
class MetadataManager {
private:
    // Mutex for thread-safe access
    std::mutex metadataMutex;
    // Map for file to list of nodes storing parts of the file
    std::unordered_map<std::string, std::vector<std::string>> fileMetadata;

public:
    // Add a new file and associate nodes to store the chunks
    void addFile(const std::string &filename, const std::vector<std::string> &nodes) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        fileMetadata[filename] = nodes;
        std::cout << "File " << filename << " added with chunks on nodes: ";
        for (const auto &node : nodes) {
            std::cout << node << " ";
        }
        std::cout << std::endl;

        // Sending message to nodes about new file
        for (const auto &node : nodes) {
            Message msg;
            msg._Type = MessageType::CreateFile;
            msg._Filename = filename;
            msg._Content = "Adding file to node";
            std::string serializedMsg = SerializeMessage(msg);
            // Here you would send `serializedMsg` to the appropriate node (communication code is assumed to be elsewhere)
            std::cout << "Sending message to " << node << ": " << serializedMsg << std::endl;
        }
    }

    // Retrieve metadata for a given file
    std::vector<std::string> getFileNodes(const std::string &filename) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        if (fileMetadata.find(filename) != fileMetadata.end()) {
            return fileMetadata[filename];
        } else {
            throw std::runtime_error("File not found in metadata.");
        }
    }

    // Remove a file from metadata
    void removeFile(const std::string &filename) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        if (fileMetadata.erase(filename)) {
            std::cout << "File " << filename << " removed from metadata." << std::endl;

            // Sending message to nodes about file removal
            Message msg;
            msg._Type = MessageType::FileCreated;
            msg._Filename = filename;
            msg._Content = "Removing file from node";
            std::string serializedMsg = SerializeMessage(msg);
            for (const auto &entry : fileMetadata) {
                for (const auto &node : entry.second) {
                    // Here you would send `serializedMsg` to the appropriate node (communication code is assumed to be elsewhere)
                    std::cout << "Sending message to " << node << ": " << serializedMsg << std::endl;
                }
            }
        } else {
            std::cout << "File " << filename << " not found in metadata." << std::endl;
        }
    }

    // Print all metadata (for debugging)
    void printMetadata() {
        std::lock_guard<std::mutex> lock(metadataMutex);
        std::cout << "Current Metadata: " << std::endl;
        for (const auto &entry : fileMetadata) {
            std::cout << "File: " << entry.first << " - Nodes: ";
            for (const auto &node : entry.second) {
                std::cout << node << " ";
            }
            std::cout << std::endl;
        }
    }
};

int main() {
    MetadataManager metadataManager;
    FileSystem fileSystem; // Using FileSystem from filesystem.h to manage file operations

    // Example of adding a file with its nodes
    std::string filename = "example.txt";
    std::vector<std::string> nodes = {"Node1", "Node3", "Node5"};
    if (fileSystem.createFile(filename)) {
        fileSystem.writeFile(filename, "This is an example file content"); // Write file content
        metadataManager.addFile(filename, nodes); // Add metadata for the file
    } else {
        std::cerr << "Failed to create file: " << filename << std::endl;
    }

    // Retrieve nodes storing the file
    try {
        std::vector<std::string> retrievedNodes = metadataManager.getFileNodes(filename);
        std::cout << "Nodes storing '" << filename << "': ";
        for (const auto &node : retrievedNodes) {
            std::cout << node << " ";
        }
        std::cout << std::endl;
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    // Read the file content
    try {
        std::string fileContent = fileSystem.readFile(filename);
        if (fileContent.empty()) {
            std::cerr << "Failed to read file or file is empty: " << filename << std::endl;
        } else {
            std::cout << "Content of '" << filename << "': " << fileContent << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    // Remove the file from metadata
    metadataManager.removeFile(filename);

    // Print current metadata
    metadataManager.printMetadata();

    return 0;
}
