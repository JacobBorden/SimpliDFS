#ifndef SIMPLIDFS_DIGEST_HPP
#define SIMPLIDFS_DIGEST_HPP

#include <array>
#include <cstddef>

namespace sgns::utils {

/// Supported hashing algorithms.
enum class HashAlgorithm { SHA256, BLAKE3 };

/// Digest size for supported algorithms (32 bytes).
inline constexpr size_t DIGEST_SIZE = 32;

using DigestArray = std::array<uint8_t, DIGEST_SIZE>;

} // namespace sgns::utils

#endif // SIMPLIDFS_DIGEST_HPP
