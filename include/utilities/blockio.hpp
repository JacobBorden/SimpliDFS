#ifndef BLOCKIO_HPP
#define BLOCKIO_HPP

#include <vector>
#include <span>
#include <cstddef> // For std::byte
#include <sodium.h> // For libsodium
#include <array>    // For std::array
#include <string>   // For std::string

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

private:
    std::vector<std::byte> buffer_;
    crypto_hash_sha256_state hash_state_; // Libsodium SHA-256 state
    bool finalized_ = false; // Tracks if finalize_hashed() has been called
};

#endif // BLOCKIO_HPP
