// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "filesystem.h" 
#include "message.h" // Including message for node communication
#include <sstream>
#include "metaserver.h"


int server() {
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
