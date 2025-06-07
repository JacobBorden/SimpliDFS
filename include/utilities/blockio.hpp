#ifndef BLOCKIO_HPP
#define BLOCKIO_HPP

#include <vector>
#include <span>
#include <cstddef> // For std::byte
#include <sodium.h> // For libsodium
#include <array>    // For std::array
#include <string>   // For std::string
#include <zstd.h>   // For zstd compression

// Define DigestResult struct
struct DigestResult {
    std::array<uint8_t, crypto_hash_sha256_BYTES> digest; // 32 bytes for SHA-256
    std::string cid;                                      // Content Identifier (CID) of the hashed data
    std::vector<std::byte> raw;
};

/**
 * @brief Buffered block processing with optional compression and encryption.
 */
class BlockIO {
public:
    /**
     * @brief Supported encryption algorithms.
     */
    enum class CipherAlgorithm { AES_256_GCM };

    /**
     * @brief Construct a new BlockIO processor.
     * @param compression_level Zstd compression level to use.
     * @param cipher_algo Encryption algorithm for encrypt/decrypt operations.
     */
    BlockIO(int compression_level = 1,
            CipherAlgorithm cipher_algo = CipherAlgorithm::AES_256_GCM);

    ~BlockIO(); ///< Destructor

    // Appends data to the internal buffer.
    void ingest(const std::byte* data, size_t size);

    // Returns a copy of the concatenated plaintext data.
    std::vector<std::byte> finalize_raw();

    // Finalizes the hash and returns the digest and raw data.
    DigestResult finalize_hashed();

    // Compression methods
    std::vector<std::byte> compress_data(const std::vector<std::byte>& plaintext_data);
    std::vector<std::byte> decompress_data(const std::vector<std::byte>& compressed_data, size_t original_size);

    // Encryption methods
    // Note: Using std::vector<unsigned char> for nonce as typically nonces are raw byte buffers.
    // And std::array<unsigned char, KEYBYTES> for key for fixed size.
    std::vector<std::byte> encrypt_data(const std::vector<std::byte>& plaintext_data,
                                        const std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& key,
                                        std::vector<unsigned char>& nonce_output);
    std::vector<std::byte> decrypt_data(const std::vector<std::byte>& ciphertext_data,
                                        const std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& key,
                                        const std::vector<unsigned char>& nonce);

private:
    std::vector<std::byte> buffer_;
    crypto_hash_sha256_state hash_state_; // Libsodium SHA-256 state
    bool finalized_ = false; // Tracks if finalize_hashed() has been called
    int compression_level_ = 1; ///< Zstd compression level
    CipherAlgorithm cipher_algo_ = CipherAlgorithm::AES_256_GCM; ///< Selected encryption algorithm
};

#endif // BLOCKIO_HPP
