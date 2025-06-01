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

class BlockIO {
public:
    BlockIO(); // Constructor
    ~BlockIO(); // Destructor

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
};

#endif // BLOCKIO_HPP
