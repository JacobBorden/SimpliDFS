#ifndef CID_UTILS_HPP
#define CID_UTILS_HPP

#include <string>
#include <vector>
#include <array>
#include <stdexcept> // For std::runtime_error
#include <cstdint>   // For uint8_t

// Assuming crypto_hash_sha256_BYTES is defined elsewhere,
// e.g., in a sodiumoxide header or a custom crypto header.
// For now, let's define it if it's not available.
#ifndef crypto_hash_sha256_BYTES
#define crypto_hash_sha256_BYTES 32
#endif

namespace sgns::utils {

extern const std::vector<uint8_t> CID_PREFIX;

/**
 * @brief Converts a SHA-256 digest to a CIDv1 string.
 * @param digest The SHA-256 digest.
 * @return The CIDv1 string.
 */
std::string digest_to_cid(const std::array<uint8_t, crypto_hash_sha256_BYTES>& digest);

/**
 * @brief Converts a CIDv1 string to a SHA-256 digest.
 * @param cid The CIDv1 string.
 * @return The SHA-256 digest.
 * @throws std::runtime_error if the CID is invalid.
 */
std::array<uint8_t, crypto_hash_sha256_BYTES> cid_to_digest(const std::string& cid);

/**
 * @brief Convert a CIDv1 string to its raw byte representation.
 */
std::vector<uint8_t> cid_to_bytes(const std::string& cid);

} // namespace sgns::utils

#endif // CID_UTILS_HPP
