#pragma once

#include <gmock/gmock.h>
#include "utilities/filesystem.h" // Original FileSystem class
#include <string>
#include <vector>
#include <cstddef> // for std::byte

// Assuming FileSystem methods are virtual. If not, this mock won't work as intended for polymorphism.
class MockFileSystem : public FileSystem {
public:
    MockFileSystem() = default;

    MOCK_METHOD(bool, createFile, (const std::string& filename), (override));
    MOCK_METHOD(bool, renameFile, (const std::string& oldFilename, const std::string& newFilename), (override));
    MOCK_METHOD(bool, writeFile, (const std::string& filename, const std::string& content), (override));
    MOCK_METHOD(std::string, readFile, (const std::string& filename), (override));
    MOCK_METHOD(bool, deleteFile, (const std::string& filename), (override));

    // fileExists is not part of the original FileSystem class in filesystem.h,
    // but it was used in Node::handleClient's ReplicateFileCommand.
    // It should be added to the FileSystem class and its mock if it's a required functionality.
    // For now, I'll add it to the mock. If it's not virtual in base, this won't override.
    // MOCK_METHOD(bool, fileExists, (const std::string& filename), (const, override)); // Needs to be const if original is

    // A simple way to simulate fileExists for tests if it's not in the base class:
    MOCK_METHOD(bool, fileExists, (const std::string& filename), ());


    // setXattr and getXattr are also in FileSystem. Add if tests need them.
    MOCK_METHOD(void, setXattr, (const std::string& filename, const std::string& attrName, const std::string& attrValue), (override));
    MOCK_METHOD(std::string, getXattr, (const std::string& filename, const std::string& attrName), (override));
};

#endif // TESTS_MOCKS_MOCK_FILESYSTEM_H
