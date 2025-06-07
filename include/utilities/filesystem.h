#pragma once
#ifndef _SIMPLIDFS_FILESYSTEM_H
#define _SIMPLIDFS_FILESYSTEM_H

#include <cstddef> // Required for std::byte
#include <mutex>   // Required for std::mutex and std::unique_lock
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector> // Required for std::vector
#include "utilities/audit_log.hpp"
#include "utilities/blockio.hpp" // For CipherAlgorithm

/**
 * @brief Manages an in-memory file system for storing file content.
 *
 * This class provides basic file operations such as creating, writing, reading,
 * and deleting files. All operations are thread-safe through the use of a
 * mutex. File content is stored as strings in an unordered map.
 */
class FileSystem {
public:
  /**
   * @brief Construct a FileSystem with processing options.
   * @param compression_level Zstd compression level.
   * @param cipher_algo Encryption algorithm for stored data.
   */
  explicit FileSystem(int compression_level = 1,
                      BlockIO::CipherAlgorithm cipher_algo =
                          BlockIO::CipherAlgorithm::AES_256_GCM);
  /**
   * @brief Creates a new, empty file in the file system.
   * If the file already exists, the operation fails.
   * @param _pFilename The name of the file to create.
   * @return True if the file was successfully created, false otherwise (e.g.,
   * if it already exists).
   */
  bool createFile(const std::string &_pFilename);

  /**
   * @brief Renames a file in the file system.
   * If the old file does not exist or the new file already exists, the
   * operation fails.
   * @param _pOldFilename The current name of the file.
   * @param _pNewFilename The new name for the file.
   * @return True if the file was successfully renamed, false otherwise.
   */
  bool renameFile(const std::string &_pOldFilename,
                  const std::string &_pNewFilename);

  /**
   * @brief Writes content to an existing file.
   * If the file does not exist, the operation fails. The existing content is
   * overwritten.
   * @param _pFilename The name of the file to write to.
   * @param _pContent The content to write into the file.
   * @return True if the content was successfully written, false otherwise
   * (e.g., if the file does not exist).
   */
  bool writeFile(const std::string &_pFilename, const std::string &_pContent);

  /**
   * @brief Reads the content of an existing file.
   * If the file does not exist, an empty string is returned.
   * @param _pFilename The name of the file to read from.
   * @return A string containing the file's content, or an empty string if the
   * file does not exist.
   */
  std::string readFile(const std::string &_pFilename);

  /**
   * @brief Deletes a file from the file system.
   * If the file exists, it is removed.
   * @param _pFilename The name of the file to delete.
   * @return True if the file was successfully deleted. Returns false if the
   * file did not exist.
   */
  bool deleteFile(const std::string &_pFilename);

  /**
   * @brief Sets an extended attribute for a file.
   * @param filename The name of the file.
   * @param attrName The name of the attribute.
   * @param attrValue The value of the attribute.
   */
  void setXattr(const std::string &filename, const std::string &attrName,
                const std::string &attrValue);

  /**
   * @brief Gets an extended attribute for a file.
   * @param filename The name of the file.
   * @param attrName The name of the attribute.
   * @return The value of the attribute, or an empty string if not found.
   */
  std::string getXattr(const std::string &filename,
                       const std::string &attrName);

  /**
   * @brief Checks if a file exists in the file system.
   * @param _pFilename The name of the file to check.
   * @return True if the file exists, false otherwise.
   */
  bool fileExists(const std::string &_pFilename) const;

  /**
   * @brief List all filenames currently stored in the filesystem.
   */
  std::vector<std::string> listFiles() const;

  /**
   * @brief Verify stored data against the hashed CID.
   * @return True if file data matches stored CID metadata.
   */
  bool verifyFileIntegrity(const std::string &filename) const;

  /**
   * @brief Create a snapshot of the current filesystem state.
   * @param name Identifier for the snapshot.
   * @return True if the snapshot was created, false if a snapshot with the
   *         same name already exists.
   */
  bool snapshotCreate(const std::string &name);

  /**
   * @brief List available snapshot names.
   */
  std::vector<std::string> snapshotList() const;

  /**
   * @brief Replace current filesystem state with the contents of a snapshot.
   * @param name The snapshot name to restore.
   * @return True if successful, false if the snapshot does not exist.
   */
  bool snapshotCheckout(const std::string &name);

  /**
   * @brief Show differences between a snapshot and the current state.
   * @param name The snapshot to compare against.
   * @return List of textual descriptions of differences.
   */
  std::vector<std::string> snapshotDiff(const std::string &name) const;

  // Return set of all CIDs referenced by files and snapshots
  std::unordered_set<std::string> getAllCids() const;

  /**
   * @brief Export a snapshot as an IPLD CAR file.
   * @param name The snapshot name to export.
   * @param carPath Destination path for the CAR file.
   * @return True on success, false if the snapshot does not exist or on I/O
   * error.
   */
  bool snapshotExportCar(const std::string &name,
                         const std::string &carPath) const;

private:
  /**
   * @brief In-memory storage for files, mapping filename to its content (now
   * binary).
   */
  std::unordered_map<std::string, std::vector<std::byte>> _Files;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      _FileXattrs;

  /// Stored snapshots of file data
  std::unordered_map<std::string,
                     std::unordered_map<std::string, std::vector<std::byte>>>
      _Snapshots;
  /// Stored snapshots of xattr metadata
  std::unordered_map<
      std::string,
      std::unordered_map<std::string,
                         std::unordered_map<std::string, std::string>>>
      _SnapshotXattrs;

  /**
   * @brief Mutex to protect the _Files map, ensuring thread-safe access to file
   * data. All public methods acquire this mutex before accessing _Files.
   */
  mutable std::mutex _Mutex;

  int compression_level_{1};
  BlockIO::CipherAlgorithm cipher_algo_{BlockIO::CipherAlgorithm::AES_256_GCM};
};

#endif
