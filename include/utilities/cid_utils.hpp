#ifndef CID_UTILS_HPP
#define CID_UTILS_HPP

#include <array>
#include <cstdint>   // For uint8_t
#include <stdexcept> // For std::runtime_error
#include <string>
#include <vector>

#include "blake3.h"
#include "digest.hpp"

// Digest size is fixed to 32 bytes for supported algorithms

namespace sgns::utils {

extern const std::vector<uint8_t> CID_PREFIX_SHA256;
extern const std::vector<uint8_t> CID_PREFIX_BLAKE3;

/**
 * @brief Converts a digest to a CIDv1 string.
 * @param digest The hash digest.
 * @return The CIDv1 string.
 */
std::string digest_to_cid(const sgns::utils::DigestArray &digest,
                          HashAlgorithm algo = HashAlgorithm::BLAKE3);

/**
 * @brief Converts a CIDv1 string to its digest.
 * @param cid The CIDv1 string.
 * @return The extracted digest.
 * @throws std::runtime_error if the CID is invalid.
 */
DigestArray cid_to_digest(const std::string &cid,
                          HashAlgorithm *algo_out = nullptr);

/**
 * @brief Convert a CIDv1 string to its raw byte representation.
 */
std::vector<uint8_t> cid_to_bytes(const std::string &cid);

} // namespace sgns::utils

#endif // CID_UTILS_HPP
