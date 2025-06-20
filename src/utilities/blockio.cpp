#include "utilities/blockio.hpp"
#include "utilities/cid_utils.hpp" // Added for digest_to_cid

#include "blake3.h"
#include "utilities/digest.hpp"
#include <algorithm> // For std::copy
#include <stdexcept> // For std::runtime_error

// Constructor
BlockIO::BlockIO(int compression_level, CipherAlgorithm cipher_algo,
                 HashAlgorithm hash_algo)
    : compression_level_(compression_level), cipher_algo_(cipher_algo),
      hash_algo_(hash_algo) {
  if (sodium_init() < 0) {
    // sodium_init() returns -1 on error, 0 on success, 1 if already initialized
    // We can choose to throw an exception or handle it differently.
    // For now, let's assume it's critical if it fails.
    throw std::runtime_error("Failed to initialize libsodium");
  }

  if (hash_algo_ == HashAlgorithm::SHA256) {
    crypto_hash_sha256_init(&sha_state_);
  } else {
    blake3_hasher_init(&blake3_state_);
  }
  finalized_ = false; // Initialize finalized_ flag
}

void BlockIO::enable_hashing(bool enable) { hashing_enabled_ = enable; }
void BlockIO::enable_compression(bool enable) { compression_enabled_ = enable; }
void BlockIO::enable_encryption(bool enable) { encryption_enabled_ = enable; }
// Destructor
BlockIO::~BlockIO() {
  // No explicit cleanup needed for hash_state_
  // sodium_close() can be called if we are sure no other part of the
  // application will use libsodium For now, we'll leave it out as it might be
  // used by other components.
}

void BlockIO::ingest(const std::byte *data, size_t size) {
  if (finalized_) {
    throw std::logic_error(
        "Cannot ingest data after finalize_hashed() has been called.");
  }
  if (data && size > 0) { // Check for null data pointer and non-zero size
    buffer_.insert(buffer_.end(), data, data + size);
    if (hash_algo_ == HashAlgorithm::SHA256) {
      crypto_hash_sha256_update(
          &sha_state_, reinterpret_cast<const unsigned char *>(data), size);
    } else {
      blake3_hasher_update(&blake3_state_,
                           reinterpret_cast<const uint8_t *>(data), size);
    }
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
  if (hash_algo_ == HashAlgorithm::SHA256) {
    crypto_hash_sha256_final(&sha_state_, result.digest.data());
  } else {
    blake3_hasher_finalize(&blake3_state_, result.digest.data(),
                           sgns::utils::DIGEST_SIZE);
  }
  result.raw = buffer_; // Copy the raw data

  // Convert digest to CID
  result.cid = sgns::utils::digest_to_cid(result.digest, hash_algo_);

  finalized_ = true; // Mark as finalized

  return result;
}

// Compression methods
std::vector<std::byte>
BlockIO::compress_data(const std::vector<std::byte> &plaintext_data) {
  if (plaintext_data.empty()) {
    return {};
  }

  size_t const cBuffSize = ZSTD_compressBound(plaintext_data.size());
  std::vector<std::byte> compressed_data(cBuffSize);

  size_t const cSize =
      ZSTD_compress(compressed_data.data(), cBuffSize, plaintext_data.data(),
                    plaintext_data.size(), compression_level_);

  if (ZSTD_isError(cSize)) {
    throw std::runtime_error(std::string("ZSTD_compress failed: ") +
                             ZSTD_getErrorName(cSize));
  }

  compressed_data.resize(cSize);
  return compressed_data;
}

std::vector<std::byte>
BlockIO::decompress_data(const std::vector<std::byte> &compressed_data,
                         size_t original_size) {
  if (compressed_data.empty()) {
    return {};
  }
  if (original_size == 0 && !compressed_data.empty()) {
    // ZSTD_getFrameContentSize can be used if the original size is not known,
    // but it requires the compressed data to contain that information.
    // For this implementation, we require original_size.
    // However, a more robust implementation might try ZSTD_getFrameContentSize.
    // For now, let's assume original_size must be provided if data is not
    // empty. Or, if original_size is 0, try to get it from the frame.
    unsigned long long const rSize = ZSTD_getFrameContentSize(
        compressed_data.data(), compressed_data.size());
    if (rSize == ZSTD_CONTENTSIZE_ERROR || rSize == ZSTD_CONTENTSIZE_UNKNOWN) {
      throw std::runtime_error(
          "ZSTD_decompress failed: original_size must be provided or "
          "retrievable from frame, and it was not.");
    }
    original_size = static_cast<size_t>(rSize);
    if (original_size == 0) { // Still 0 after trying to get from frame (e.g.
                              // for empty original data)
      return {};
    }
  }

  std::vector<std::byte> decompressed_data(original_size);

  size_t const dSize =
      ZSTD_decompress(decompressed_data.data(), original_size,
                      compressed_data.data(), compressed_data.size());

  if (ZSTD_isError(dSize)) {
    throw std::runtime_error(std::string("ZSTD_decompress failed: ") +
                             ZSTD_getErrorName(dSize));
  }

  if (dSize != original_size) {
    // This case should ideally not happen if original_size was correct and
    // ZSTD_decompress succeeded. However, it's a good sanity check.
    throw std::runtime_error(
        "ZSTD_decompress failed: output size does not match original size.");
  }

  // The vector is already sized to original_size, no resize needed if dSize ==
  // original_size. If ZSTD_decompress can return a dSize smaller than
  // original_size for some valid cases (e.g. if original_size was an upper
  // bound), then resize might be needed: decompressed_data.resize(dSize); But
  // typically for zstd, you decompress into a buffer of known original size.

  return decompressed_data;
}

// Encryption methods
std::vector<std::byte> BlockIO::encrypt_data(
    const std::vector<std::byte> &plaintext_data,
    const std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
        &key,
    std::vector<unsigned char> &nonce_output) {
  if (cipher_algo_ == CipherAlgorithm::AES_256_GCM &&
      crypto_aead_aes256gcm_is_available()) {
    nonce_output.resize(crypto_aead_aes256gcm_NPUBBYTES);
    randombytes_buf(nonce_output.data(), nonce_output.size());

    std::vector<std::byte> ciphertext(plaintext_data.size() +
                                      crypto_aead_aes256gcm_ABYTES);
    unsigned long long ciphertext_len;
    int result = crypto_aead_aes256gcm_encrypt(
        reinterpret_cast<unsigned char *>(ciphertext.data()), &ciphertext_len,
        reinterpret_cast<const unsigned char *>(plaintext_data.data()),
        plaintext_data.size(), nullptr, 0, nullptr, nonce_output.data(),
        key.data());
    if (result != 0) {
      throw std::runtime_error("Encryption failed.");
    }
    ciphertext.resize(static_cast<size_t>(ciphertext_len));
    return ciphertext;
  }

  nonce_output.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  randombytes_buf(nonce_output.data(), nonce_output.size());

  std::vector<std::byte> ciphertext(plaintext_data.size() +
                                    crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long ciphertext_len;
  int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
      reinterpret_cast<unsigned char *>(ciphertext.data()), &ciphertext_len,
      reinterpret_cast<const unsigned char *>(plaintext_data.data()),
      plaintext_data.size(), nullptr, 0, nullptr, nonce_output.data(),
      key.data());
  if (result != 0) {
    throw std::runtime_error("Encryption failed.");
  }
  ciphertext.resize(static_cast<size_t>(ciphertext_len));
  return ciphertext;
}

std::vector<std::byte> BlockIO::decrypt_data(
    const std::vector<std::byte> &ciphertext_data,
    const std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
        &key,
    const std::vector<unsigned char> &nonce) {
  if (cipher_algo_ == CipherAlgorithm::AES_256_GCM &&
      crypto_aead_aes256gcm_is_available()) {
    if (nonce.size() != crypto_aead_aes256gcm_NPUBBYTES) {
      throw std::runtime_error("Invalid nonce size for decryption.");
    }
    if (ciphertext_data.size() < crypto_aead_aes256gcm_ABYTES) {
      throw std::runtime_error("Invalid ciphertext: too short to contain MAC.");
    }
    std::vector<std::byte> decrypted(ciphertext_data.size() -
                                     crypto_aead_aes256gcm_ABYTES);
    unsigned long long decrypted_len;
    int result = crypto_aead_aes256gcm_decrypt(
        reinterpret_cast<unsigned char *>(decrypted.data()), &decrypted_len,
        nullptr,
        reinterpret_cast<const unsigned char *>(ciphertext_data.data()),
        ciphertext_data.size(), nullptr, 0, nonce.data(), key.data());
    if (result != 0) {
      throw std::runtime_error(
          "Decryption failed. Ciphertext might be invalid or tampered.");
    }
    decrypted.resize(static_cast<size_t>(decrypted_len));
    return decrypted;
  }

  if (nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES) {
    throw std::runtime_error("Invalid nonce size for decryption.");
  }
  if (ciphertext_data.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) {
    throw std::runtime_error("Invalid ciphertext: too short to contain MAC.");
  }
  std::vector<std::byte> decrypted(ciphertext_data.size() -
                                   crypto_aead_xchacha20poly1305_ietf_ABYTES);
  unsigned long long decrypted_len;
  int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
      reinterpret_cast<unsigned char *>(decrypted.data()), &decrypted_len,
      nullptr, reinterpret_cast<const unsigned char *>(ciphertext_data.data()),
      ciphertext_data.size(), nullptr, 0, nonce.data(), key.data());
  if (result != 0) {
    throw std::runtime_error(
        "Decryption failed. Ciphertext might be invalid or tampered.");
  }
  decrypted.resize(static_cast<size_t>(decrypted_len));
  return decrypted;
}

BlockIO::PipelineResult BlockIO::finalize_pipeline(
    const std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
        *key) {
  if (finalized_) {
    throw std::logic_error("finalize_pipeline() already called.");
  }

  PipelineResult result{};
  std::vector<std::byte> data = buffer_;

  if (compression_enabled_) {
    data = compress_data(data);
  }

  if (encryption_enabled_) {
    if (!key) {
      throw std::runtime_error("Encryption key required");
    }
    std::vector<unsigned char> nonce;
    data = encrypt_data(data, *key, nonce);
    result.nonce = nonce;
  }

  if (hashing_enabled_) {
    if (hash_algo_ == HashAlgorithm::SHA256) {
      crypto_hash_sha256(result.digest.data(),
                         reinterpret_cast<const unsigned char *>(data.data()),
                         data.size());
    } else {
      blake3_hasher hasher;
      blake3_hasher_init(&hasher);
      blake3_hasher_update(
          &hasher, reinterpret_cast<const uint8_t *>(data.data()), data.size());
      blake3_hasher_finalize(&hasher, result.digest.data(),
                             sgns::utils::DIGEST_SIZE);
    }
    result.cid = sgns::utils::digest_to_cid(result.digest, hash_algo_);
  }

  result.data = data;
  finalized_ = true;
  return result;
}
