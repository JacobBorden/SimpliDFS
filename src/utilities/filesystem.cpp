#include "utilities/filesystem.h"
#include "utilities/logger.h" // Include the Logger header
#include "utilities/blockio.hpp" // Already in .h, but good for explicitness or if .h changes
#include "utilities/cid_utils.hpp" // For digest_to_cid, though BlockIO handles it internally

#include <string>
#include <vector>
#include <cstddef> // For std::byte
#include <stdexcept> // For std::runtime_error
#include <algorithm> // For std::copy, std::transform
#include <iterator>  // For std::back_inserter

// Helper function to convert string to vector<byte>
inline std::vector<std::byte> string_to_bytes(const std::string& str) {
    std::vector<std::byte> bytes(str.size());
    std::transform(str.begin(), str.end(), bytes.begin(), [](char c) {
        return static_cast<std::byte>(c);
    });
    return bytes;
}

// Helper function to convert vector<byte> to string
inline std::string bytes_to_string(const std::vector<std::byte>& bytes) {
    std::string str(bytes.size(), '\0');
    std::transform(bytes.begin(), bytes.end(), str.begin(), [](std::byte b) {
        return static_cast<char>(b);
    });
    return str;
}


bool FileSystem::createFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to create file that already exists: " + _pFilename);
		return false;
    }
	_Files[_pFilename] = {}; // Store empty vector<byte>
    Logger::getInstance().log(LogLevel::INFO, "File created: " + _pFilename);
	return true;
		
}


bool FileSystem::writeFile(const std::string& _pFilename, const std::string& _pContent)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::ERROR, "Attempted to write to non-existent file: " + _pFilename);
		return false;
    }

    try {
        BlockIO localBlockIO; // Create a local BlockIO instance for this operation

        // 1. Convert content to std::vector<std::byte>
        std::vector<std::byte> rawData = string_to_bytes(_pContent);

        // 2. Ingest raw data for hashing and get CID
        // Create a separate BlockIO for hashing to ensure clean state if needed,
        // though ingest/finalize_hashed on the same localBlockIO should be fine if it resets or is single-use.
        // For safety, let's assume BlockIO's finalize_hashed might leave it in a state
        // not suitable for further data processing operations like encrypt/compress on its internal buffer.
        // The current BlockIO API takes data as parameter for encrypt/compress, so it's fine.
        BlockIO hasher;
        hasher.ingest(rawData.data(), rawData.size());
        DigestResult hashResult = hasher.finalize_hashed();
        std::string cid = hashResult.cid;

        // 3. Define placeholder encryption key
        std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key;
        key.fill('A'); // Placeholder - MUST be replaced with proper key management

        // 4. Encrypt the raw data
        std::vector<unsigned char> nonce;
        std::vector<std::byte> encryptedData = localBlockIO.encrypt_data(rawData, key, nonce);

        // 5. Compress the encrypted data
        std::vector<std::byte> compressedData = localBlockIO.compress_data(encryptedData);

        // 6. Store compressed data
        _Files[_pFilename] = compressedData;

        // 7. Store metadata
        setXattr(_pFilename, "user.cid", cid);
        // Convert nonce to a string for storage (e.g., hex or base64, here simple char conversion)
        std::string nonce_str;
        nonce_str.reserve(nonce.size());
        for(unsigned char uc : nonce) { nonce_str.push_back(static_cast<char>(uc)); } // Simple byte to char
        setXattr(_pFilename, "user.nonce", nonce_str);
        setXattr(_pFilename, "user.original_size", std::to_string(rawData.size()));
        setXattr(_pFilename, "user.encrypted_size", std::to_string(encryptedData.size()));

        Logger::getInstance().log(LogLevel::INFO, "File written with encryption/compression: " + _pFilename + ", CID: " + cid);
        return true;

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "FileSystem::writeFile failed for " + _pFilename + ": " + e.what());
        // Potentially revert changes or mark file as corrupted if partial write occurred.
        // For now, just return false. If _Files[_pFilename] was updated, it might hold partial data.
        // A robust implementation might remove _Files[_pFilename] or store its old value.
        return false;
    }
}


std::string FileSystem::readFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::ERROR, "Attempted to read non-existent file: " + _pFilename);
		return "";
    }

    try {
        const std::vector<std::byte>& compressedData = _Files.at(_pFilename);
        if (compressedData.empty() && getXattr(_pFilename, "user.cid").empty()) {
             // If it's an empty file created by createFile, it has no xattrs and empty data.
            Logger::getInstance().log(LogLevel::INFO, "File read (empty, as created): " + _pFilename);
            return "";
        }


        // 1. Retrieve metadata
        std::string cid = getXattr(_pFilename, "user.cid");
        std::string nonce_str = getXattr(_pFilename, "user.nonce");
        std::string original_size_str = getXattr(_pFilename, "user.original_size");
        std::string encrypted_size_str = getXattr(_pFilename, "user.encrypted_size");

        if (cid.empty() || nonce_str.empty() || original_size_str.empty() || encrypted_size_str.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "Missing metadata for file: " + _pFilename);
            // This might be an old file not written with the new pipeline, or corruption.
            // Depending on policy, could return raw data if available and no CID, or error.
            // For now, assume if metadata is missing, it's an error for this pipeline.
            // If any critical metadata is missing, we cannot proceed with the new pipeline.
            Logger::getInstance().log(LogLevel::ERROR, "File " + _pFilename + " is missing critical pipeline metadata (cid, nonce, original_size, or encrypted_size). Cannot process as new format.");
            throw std::runtime_error("Missing required metadata for new format decryption/decompression.");
        }

        // Convert nonce string (raw bytes) to vector<unsigned char>
        std::vector<unsigned char> nonce(nonce_str.length());
        std::transform(nonce_str.begin(), nonce_str.end(), nonce.begin(), [](char c){
            return static_cast<unsigned char>(c);
        });

        // size_t original_raw_size = std::stoul(original_size_str); // size of original plaintext data
        size_t encrypted_size = std::stoul(encrypted_size_str); // size of data before compression (i.e. encrypted data size)


        // 2. Define placeholder encryption key (must match writeFile)
        std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key;
        key.fill('A'); // Placeholder

        BlockIO localBlockIO; // Create a local BlockIO instance

        // 3. Decompress the data
        // The 'original_size' for decompress_data here is the size of the data *before* compression,
        // which is the encrypted_size.
        std::vector<std::byte> decompressedEncryptedData = localBlockIO.decompress_data(compressedData, encrypted_size);

        // 4. Decrypt the data
        std::vector<std::byte> decryptedRawData = localBlockIO.decrypt_data(decompressedEncryptedData, key, nonce);

        // 5. Verify the hash (CID)
        BlockIO hashVerifierBlockIO;
        hashVerifierBlockIO.ingest(decryptedRawData.data(), decryptedRawData.size());
        DigestResult verificationResult = hashVerifierBlockIO.finalize_hashed();
        if (verificationResult.cid != cid) {
            Logger::getInstance().log(LogLevel::ERROR, "CID mismatch after decryption for file: " + _pFilename + ". Expected: " + cid + ", Got: " + verificationResult.cid);
            throw std::runtime_error("CID mismatch after decryption! Data may be corrupted or key is wrong.");
        }

        Logger::getInstance().log(LogLevel::INFO, "File read with decryption/decompression: " + _pFilename + ", CID: " + cid);

        // 6. Convert decrypted data to string and return
        return bytes_to_string(decryptedRawData);

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "FileSystem::readFile failed for " + _pFilename + ": " + e.what());
        return ""; // Return empty string on error
    }
}

bool FileSystem::deleteFile(const std::string& _pFilename) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.count(_pFilename)) {
        _Files.erase(_pFilename);
        _FileXattrs.erase(_pFilename); // Also remove associated xattrs
        Logger::getInstance().log(LogLevel::INFO, "File deleted: " + _pFilename);
        return true; // Successfully deleted
    }
    Logger::getInstance().log(LogLevel::WARN, "Attempted to delete non-existent file: " + _pFilename);
    return false; // File did not exist
}

bool FileSystem::renameFile(const std::string& _pOldFilename, const std::string& _pNewFilename) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.find(_pOldFilename) == _Files.end()) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to rename non-existent file: " + _pOldFilename);
        return false; // Old file doesn't exist
    }
    if (_Files.find(_pNewFilename) != _Files.end()) {
        // Overwriting an existing file via rename is typically disallowed or requires a special flag.
        // For now, let's disallow to prevent accidental data loss.
        Logger::getInstance().log(LogLevel::WARN, "Attempted to rename to an already existing file: " + _pNewFilename);
        return false; // New file already exists
    }

    // Perform the rename for file content
    _Files[_pNewFilename] = std::move(_Files[_pOldFilename]);
    _Files.erase(_pOldFilename);

    // Transfer xattrs
    if (_FileXattrs.count(_pOldFilename)) {
        _FileXattrs[_pNewFilename] = std::move(_FileXattrs[_pOldFilename]);
        _FileXattrs.erase(_pOldFilename);
    }

    Logger::getInstance().log(LogLevel::INFO, "File renamed from " + _pOldFilename + " to " + _pNewFilename);
    return true;
}

void FileSystem::setXattr(const std::string& filename, const std::string& attrName, const std::string& attrValue) {
    // This function is called internally by writeFile which already holds the lock and ensures file exists in _Files.
    if (_Files.find(filename) == _Files.end()) {
        // This check is a safeguard. Under normal operation by writeFile, file should exist.
        Logger::getInstance().log(LogLevel::ERROR, "FileSystem::setXattr called for a file not in _Files: " + filename);
        return;
    }
    _FileXattrs[filename][attrName] = attrValue;
    // Logger::getInstance().log(LogLevel::DEBUG, "xattr set for file: " + filename + ", Attribute: " + attrName);
}

std::string FileSystem::getXattr(const std::string& filename, const std::string& attrName) {
    // This function is called internally by readFile which already holds the lock.
    auto it = _FileXattrs.find(filename);
    if (it != _FileXattrs.end()) {
        auto attrIt = it->second.find(attrName);
        if (attrIt != it->second.end()) {
            // Logger::getInstance().log(LogLevel::INFO, "xattr retrieved for file: " + filename + ", Attribute: " + attrName);
            return attrIt->second;
        }
    }
    // Logger::getInstance().log(LogLevel::INFO, "xattr not found for file: " + filename + ", Attribute: " + attrName);
    return "";
}
