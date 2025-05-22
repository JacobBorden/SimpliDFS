#pragma once
#ifndef _SIMPLIDFS_FILESYSTEM_H
#define _SIMPLIDFS_FILESYSTEM_H

#include <string>
#include <unordered_map>
#include <mutex> // Required for std::mutex and std::unique_lock

/**
 * @brief Manages an in-memory file system for storing file content.
 * 
 * This class provides basic file operations such as creating, writing, reading,
 * and deleting files. All operations are thread-safe through the use of a mutex.
 * File content is stored as strings in an unordered map.
 */
class FileSystem {
public:
    /**
     * @brief Creates a new, empty file in the file system.
     * If the file already exists, the operation fails.
     * @param _pFilename The name of the file to create.
     * @return True if the file was successfully created, false otherwise (e.g., if it already exists).
     */
    bool createFile(const std::string& _pFilename);

    /**
     * @brief Writes content to an existing file.
     * If the file does not exist, the operation fails. The existing content is overwritten.
     * @param _pFilename The name of the file to write to.
     * @param _pContent The content to write into the file.
     * @return True if the content was successfully written, false otherwise (e.g., if the file does not exist).
     */
    bool writeFile(const std::string& _pFilename, const std::string& _pContent);

    /**
     * @brief Reads the content of an existing file.
     * If the file does not exist, an empty string is returned.
     * @param _pFilename The name of the file to read from.
     * @return A string containing the file's content, or an empty string if the file does not exist.
     */
    std::string readFile(const std::string& _pFilename);

    /**
     * @brief Deletes a file from the file system.
     * If the file exists, it is removed.
     * @param _pFilename The name of the file to delete.
     * @return True if the file was successfully deleted. Returns false if the file did not exist.
     */
    bool deleteFile(const std::string& _pFilename);

private:
    /**
     * @brief In-memory storage for files, mapping filename to its content.
     */
    std::unordered_map<std::string, std::string> _Files;

    /**
     * @brief Mutex to protect the _Files map, ensuring thread-safe access to file data.
     * All public methods acquire this mutex before accessing _Files.
     */
    std::mutex _Mutex; 
};


#endif
