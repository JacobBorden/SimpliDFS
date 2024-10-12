// Metadata class to manage file locations and details
#include "filesystem.h"
#include "message.h"
#include <vector>
#include <string>
#include <iostream>


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