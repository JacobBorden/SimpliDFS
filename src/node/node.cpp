#include "node/node.h" // For Node class definition
#include "utilities/filesystem.h" // For FileSystem operations

// Implementation for Node::checkFileExistsOnNode
bool Node::checkFileExistsOnNode(const std::string& filename) const { // Made const again
    // Calls the new FileSystem::fileExists method which is also const.
    return fileSystem.fileExists(filename);
}

// To ensure this file is not empty if other methods are moved here later.
// void Node::someOtherMethod() { /* ... */ }
