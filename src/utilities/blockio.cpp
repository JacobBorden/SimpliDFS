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
