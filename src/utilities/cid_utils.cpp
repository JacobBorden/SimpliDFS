#include "utilities/cid_utils.hpp"
#include <vector>
#include <stdexcept> // For std::runtime_error
#include "cppcodec/base32_rfc4648.hpp" // For Base32 encoding/decoding

namespace sgns::utils {

// CIDv1 (0x01)
// multicodec for DAG-PB (0x70)
// multicodec for SHA2-256 (0x12)
// length of hash (0x20)
const std::vector<uint8_t> CID_PREFIX = {0x01, 0x70, 0x12, 0x20};

std::string digest_to_cid(const std::array<uint8_t, crypto_hash_sha256_BYTES>& digest) {
    std::vector<uint8_t> bytes_to_encode;
    bytes_to_encode.insert(bytes_to_encode.end(), CID_PREFIX.begin(), CID_PREFIX.end());
    bytes_to_encode.insert(bytes_to_encode.end(), digest.begin(), digest.end());

    return cppcodec::base32_rfc4648::encode(bytes_to_encode);
}

std::array<uint8_t, crypto_hash_sha256_BYTES> cid_to_digest(const std::string& cid) {
    if (cid.empty()) {
        throw std::runtime_error("CID string cannot be empty.");
    }

    // Base32 decoding typically expects uppercase, but RFC4648 says it can be upper or lower.
    // cpp-base32 handles both, but let's ensure we pass what it expects if there are issues.
    // For now, we assume it handles mixed case or we convert to uppercase if necessary.
    std::vector<uint8_t> decoded_bytes;
    try {
        // cppcodec's decode function takes a pointer and a size, or a range.
        // Using a range-based version or ensuring the input is null-terminated if using char*
        decoded_bytes = cppcodec::base32_rfc4648::decode(cid.data(), cid.length());
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to decode Base32 CID: " + std::string(e.what()));
    }


    if (decoded_bytes.size() < CID_PREFIX.size()) {
        throw std::runtime_error("Invalid CID: Decoded data too short to contain prefix.");
    }

    // Verify prefix
    for (size_t i = 0; i < CID_PREFIX.size(); ++i) {
        if (decoded_bytes[i] != CID_PREFIX[i]) {
            throw std::runtime_error("Invalid CID: Prefix mismatch.");
        }
    }

    size_t expected_digest_size = crypto_hash_sha256_BYTES;
    if (decoded_bytes.size() != CID_PREFIX.size() + expected_digest_size) {
        throw std::runtime_error("Invalid CID: Decoded data length does not match expected digest size.");
    }

    std::array<uint8_t, crypto_hash_sha256_BYTES> digest;
    std::copy(decoded_bytes.begin() + CID_PREFIX.size(), decoded_bytes.end(), digest.begin());

    return digest;
}

std::vector<uint8_t> cid_to_bytes(const std::string& cid) {
    auto digest = cid_to_digest(cid);
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), CID_PREFIX.begin(), CID_PREFIX.end());
    bytes.insert(bytes.end(), digest.begin(), digest.end());
    return bytes;
}

} // namespace sgns::utils
