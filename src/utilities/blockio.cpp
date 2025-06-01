#include "utilities/blockio.hpp"
#include "utilities/cid_utils.hpp" // Added for digest_to_cid

#include <algorithm> // For std::copy
#include <stdexcept> // For std::runtime_error

// Constructor
BlockIO::BlockIO() {
    if (sodium_init() < 0) {
        // sodium_init() returns -1 on error, 0 on success, 1 if already initialized
        // We can choose to throw an exception or handle it differently.
        // For now, let's assume it's critical if it fails.
        throw std::runtime_error("Failed to initialize libsodium");
    }

    // Explicitly zero-initialize hash_state_ before libsodium init.
    // This is speculative and typically should not be needed.
    // Using unsigned char* for byte-wise manipulation.
    unsigned char * const p_state_bytes = reinterpret_cast<unsigned char *>(&hash_state_);
    for (size_t i = 0; i < sizeof(crypto_hash_sha256_state); ++i) {
        p_state_bytes[i] = 0U;
    }

    crypto_hash_sha256_init(&hash_state_);
    finalized_ = false; // Initialize finalized_ flag
}

// Destructor
BlockIO::~BlockIO() {
    // No explicit cleanup needed for hash_state_
    // sodium_close() can be called if we are sure no other part of the application will use libsodium
    // For now, we'll leave it out as it might be used by other components.
}

void BlockIO::ingest(const std::byte* data, size_t size) {
    if (finalized_) {
        throw std::logic_error("Cannot ingest data after finalize_hashed() has been called.");
    }
    if (data && size > 0) { // Check for null data pointer and non-zero size
        buffer_.insert(buffer_.end(), data, data + size);
        // Update SHA-256 hash state
        crypto_hash_sha256_update(&hash_state_, reinterpret_cast<const unsigned char*>(data), size);
    }
}

std::vector<std::byte> BlockIO::finalize_raw() {
    if (finalized_) {
        // Or, alternatively, allow finalize_raw() even after hashing,
        // as it doesn't affect the hash state.
        // For now, let's keep it consistent with ingest and disallow.
        // However, the requirement was about ingest and re-finalizing.
        // Let's assume finalize_raw() should still work.
    }
    // Return a copy of the buffer.
    return buffer_;
}

DigestResult BlockIO::finalize_hashed() {
    if (finalized_) {
        throw std::logic_error("finalize_hashed() already called.");
    }

    DigestResult result;
    crypto_hash_sha256_final(&hash_state_, result.digest.data());
    result.raw = buffer_; // Copy the raw data

    // Convert digest to CID
    result.cid = sgns::utils::digest_to_cid(result.digest);

    finalized_ = true; // Mark as finalized

    return result;
}

// Compression methods
std::vector<std::byte> BlockIO::compress_data(const std::vector<std::byte>& plaintext_data) {
    if (plaintext_data.empty()) {
        return {};
    }

    size_t const cBuffSize = ZSTD_compressBound(plaintext_data.size());
    std::vector<std::byte> compressed_data(cBuffSize);

    size_t const cSize = ZSTD_compress(
        compressed_data.data(), cBuffSize,
        plaintext_data.data(), plaintext_data.size(),
        1 // Default compression level
    );

    if (ZSTD_isError(cSize)) {
        throw std::runtime_error(std::string("ZSTD_compress failed: ") + ZSTD_getErrorName(cSize));
    }

    compressed_data.resize(cSize);
    return compressed_data;
}

std::vector<std::byte> BlockIO::decompress_data(const std::vector<std::byte>& compressed_data, size_t original_size) {
    if (compressed_data.empty()) {
        return {};
    }
    if (original_size == 0 && !compressed_data.empty()) {
        // ZSTD_getFrameContentSize can be used if the original size is not known,
        // but it requires the compressed data to contain that information.
        // For this implementation, we require original_size.
        // However, a more robust implementation might try ZSTD_getFrameContentSize.
        // For now, let's assume original_size must be provided if data is not empty.
        // Or, if original_size is 0, try to get it from the frame.
        unsigned long long const rSize = ZSTD_getFrameContentSize(compressed_data.data(), compressed_data.size());
        if (rSize == ZSTD_CONTENTSIZE_ERROR || rSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            throw std::runtime_error("ZSTD_decompress failed: original_size must be provided or retrievable from frame, and it was not.");
        }
        original_size = static_cast<size_t>(rSize);
        if (original_size == 0) { // Still 0 after trying to get from frame (e.g. for empty original data)
             return {};
        }
    }


    std::vector<std::byte> decompressed_data(original_size);

    size_t const dSize = ZSTD_decompress(
        decompressed_data.data(), original_size,
        compressed_data.data(), compressed_data.size()
    );

    if (ZSTD_isError(dSize)) {
        throw std::runtime_error(std::string("ZSTD_decompress failed: ") + ZSTD_getErrorName(dSize));
    }

    if (dSize != original_size) {
        // This case should ideally not happen if original_size was correct and ZSTD_decompress succeeded.
        // However, it's a good sanity check.
        throw std::runtime_error("ZSTD_decompress failed: output size does not match original size.");
    }

    // The vector is already sized to original_size, no resize needed if dSize == original_size.
    // If ZSTD_decompress can return a dSize smaller than original_size for some valid cases
    // (e.g. if original_size was an upper bound), then resize might be needed:
    // decompressed_data.resize(dSize);
    // But typically for zstd, you decompress into a buffer of known original size.

    return decompressed_data;
}

// Encryption methods
std::vector<std::byte> BlockIO::encrypt_data(const std::vector<std::byte>& plaintext_data,
                                             const std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& key,
                                             std::vector<unsigned char>& nonce_output) {
    if (!crypto_aead_aes256gcm_is_available()) {
        throw std::runtime_error("AES-256-GCM is not available on this CPU.");
    }

    nonce_output.resize(crypto_aead_aes256gcm_NPUBBYTES);
    randombytes_buf(nonce_output.data(), nonce_output.size());

    std::vector<std::byte> ciphertext(plaintext_data.size() + crypto_aead_aes256gcm_ABYTES);
    unsigned long long ciphertext_len;

    int result = crypto_aead_aes256gcm_encrypt(
        reinterpret_cast<unsigned char*>(ciphertext.data()), &ciphertext_len,
        reinterpret_cast<const unsigned char*>(plaintext_data.data()), plaintext_data.size(),
        nullptr, 0, // No additional authenticated data
        nullptr,    // Must be NULL for this function (according to docs for some versions)
        nonce_output.data(), key.data()
    );

    if (result != 0) {
        throw std::runtime_error("Encryption failed.");
    }
    ciphertext.resize(static_cast<size_t>(ciphertext_len));
    return ciphertext;
}

std::vector<std::byte> BlockIO::decrypt_data(const std::vector<std::byte>& ciphertext_data,
                                             const std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& key,
                                             const std::vector<unsigned char>& nonce) {
    if (!crypto_aead_aes256gcm_is_available()) {
        throw std::runtime_error("AES-256-GCM is not available on this CPU.");
    }

    if (nonce.size() != crypto_aead_aes256gcm_NPUBBYTES) {
        throw std::runtime_error("Invalid nonce size for decryption.");
    }

    if (ciphertext_data.size() < crypto_aead_aes256gcm_ABYTES) {
        throw std::runtime_error("Invalid ciphertext: too short to contain MAC.");
    }

    std::vector<std::byte> decrypted_data(ciphertext_data.size() - crypto_aead_aes256gcm_ABYTES);
    unsigned long long decrypted_len;

    int result = crypto_aead_aes256gcm_decrypt(
        reinterpret_cast<unsigned char*>(decrypted_data.data()), &decrypted_len,
        nullptr, // Must be NULL (not used for output by this function)
        reinterpret_cast<const unsigned char*>(ciphertext_data.data()), ciphertext_data.size(),
        nullptr, 0, // No additional authenticated data
        nonce.data(), key.data()
    );

    if (result != 0) {
        throw std::runtime_error("Decryption failed. Ciphertext might be invalid or tampered.");
    }
    decrypted_data.resize(static_cast<size_t>(decrypted_len));
    return decrypted_data;
}
