#include "utilities/cid_utils.hpp"
#include "blake3.h"
#include "cppcodec/base32_rfc4648.hpp" // For Base32 encoding/decoding
#include "utilities/digest.hpp"
#include <stdexcept> // For std::runtime_error
#include <vector>

namespace sgns::utils {

// CIDv1 (0x01)
// multicodec for DAG-PB (0x70)
// multicodec for SHA2-256 (0x12) or BLAKE3 (0x1e)
// length of hash (0x20)
const std::vector<uint8_t> CID_PREFIX_SHA256 = {0x01, 0x70, 0x12, 0x20};
const std::vector<uint8_t> CID_PREFIX_BLAKE3 = {0x01, 0x70, 0x1e, 0x20};

std::string digest_to_cid(const sgns::utils::DigestArray &digest,
                          HashAlgorithm algo) {
  const auto &prefix =
      algo == HashAlgorithm::SHA256 ? CID_PREFIX_SHA256 : CID_PREFIX_BLAKE3;
  std::vector<uint8_t> bytes_to_encode;
  bytes_to_encode.insert(bytes_to_encode.end(), prefix.begin(), prefix.end());
  bytes_to_encode.insert(bytes_to_encode.end(), digest.begin(), digest.end());

  return cppcodec::base32_rfc4648::encode(bytes_to_encode);
}

DigestArray cid_to_digest(const std::string &cid, HashAlgorithm *algo_out) {
  if (cid.empty()) {
    throw std::runtime_error("CID string cannot be empty.");
  }

  // Base32 decoding typically expects uppercase, but RFC4648 says it can be
  // upper or lower. cpp-base32 handles both, but let's ensure we pass what it
  // expects if there are issues. For now, we assume it handles mixed case or we
  // convert to uppercase if necessary.
  std::vector<uint8_t> decoded_bytes;
  try {
    // cppcodec's decode function takes a pointer and a size, or a range.
    // Using a range-based version or ensuring the input is null-terminated if
    // using char*
    decoded_bytes = cppcodec::base32_rfc4648::decode(cid.data(), cid.length());
  } catch (const std::exception &e) {
    throw std::runtime_error("Failed to decode Base32 CID: " +
                             std::string(e.what()));
  }

  if (decoded_bytes.size() < CID_PREFIX_BLAKE3.size()) {
    throw std::runtime_error(
        "Invalid CID: Decoded data too short to contain prefix.");
  }

  const std::vector<uint8_t> *prefix = nullptr;
  HashAlgorithm algo;
  if (std::equal(decoded_bytes.begin(),
                 decoded_bytes.begin() + CID_PREFIX_SHA256.size(),
                 CID_PREFIX_SHA256.begin())) {
    prefix = &CID_PREFIX_SHA256;
    algo = HashAlgorithm::SHA256;
  } else if (std::equal(decoded_bytes.begin(),
                        decoded_bytes.begin() + CID_PREFIX_BLAKE3.size(),
                        CID_PREFIX_BLAKE3.begin())) {
    prefix = &CID_PREFIX_BLAKE3;
    algo = HashAlgorithm::BLAKE3;
  } else {
    throw std::runtime_error("Invalid CID: Prefix mismatch.");
  }

  size_t expected_digest_size = sgns::utils::DIGEST_SIZE;
  if (decoded_bytes.size() != prefix->size() + expected_digest_size) {
    throw std::runtime_error("Invalid CID: Decoded data length does not match "
                             "expected digest size.");
  }

  DigestArray digest;
  std::copy(decoded_bytes.begin() + prefix->size(), decoded_bytes.end(),
            digest.begin());

  if (algo_out) {
    *algo_out = algo;
  }

  return digest;
}

std::vector<uint8_t> cid_to_bytes(const std::string &cid) {
  HashAlgorithm algo;
  auto digest = cid_to_digest(cid, &algo);
  const auto &prefix =
      algo == HashAlgorithm::SHA256 ? CID_PREFIX_SHA256 : CID_PREFIX_BLAKE3;
  std::vector<uint8_t> bytes;
  bytes.insert(bytes.end(), prefix.begin(), prefix.end());
  bytes.insert(bytes.end(), digest.begin(), digest.end());
  return bytes;
}

} // namespace sgns::utils
