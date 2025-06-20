#ifndef BLOCKIO_HPP
#define BLOCKIO_HPP

#include <array>    // For std::array
#include <cstddef>  // For std::byte
#include <sodium.h> // For libsodium
#include <span>
#include <string> // For std::string
#include <vector>
#include <zstd.h> // For zstd compression

// Define DigestResult struct
struct DigestResult {
  std::array<uint8_t, crypto_hash_sha256_BYTES> digest; // 32 bytes for SHA-256
  std::string cid; // Content Identifier (CID) of the hashed data
  std::vector<std::byte> raw;
};

/**
 * @brief Buffered block processing with a configurable pipeline supporting
 * hashing, compression and encryption.
 */
class BlockIO {
public:
  /**
   * @brief Supported encryption algorithms.
   */
  enum class CipherAlgorithm { XCHACHA20_POLY1305, AES_256_GCM };

  /**
   * @brief Construct a new BlockIO processor.
   * @param compression_level Zstd compression level to use.
   * @param cipher_algo Encryption algorithm for encrypt/decrypt operations.
   */
  BlockIO(int compression_level = 1,
          CipherAlgorithm cipher_algo = CipherAlgorithm::XCHACHA20_POLY1305);

  /** Enable or disable hashing stage. */
  void enable_hashing(bool enable);
  /** Enable or disable compression stage. */
  void enable_compression(bool enable);
  /** Enable or disable encryption stage. */
  void enable_encryption(bool enable);

  ~BlockIO(); ///< Destructor

  // Appends data to the internal buffer.
  void ingest(const std::byte *data, size_t size);

  // Returns a copy of the concatenated plaintext data.
  std::vector<std::byte> finalize_raw();

  // Finalizes the hash and returns the digest and raw data.
  DigestResult finalize_hashed();

  /** Result returned by finalize_pipeline(). */
  struct PipelineResult {
    std::vector<std::byte> data;      ///< Processed output data
    std::vector<unsigned char> nonce; ///< Nonce used for encryption
    std::array<uint8_t, crypto_hash_sha256_BYTES> digest; ///< Hash digest
    std::string cid; ///< CID derived from digest
  };

  /**
   * @brief Finalize buffered data through the configured pipeline.
   *
   * The order of operations is compression, then encryption, then hashing.
   * Each step is optional based on the enabled flags.
   *
   * @param key Encryption key if encryption is enabled.
   * @return PipelineResult containing processed data and optional metadata.
   * @throw std::logic_error If finalize_pipeline() is called more than once.
   * @throw std::runtime_error If encryption enabled but key is missing or
   *         encryption fails.
   */
  PipelineResult finalize_pipeline(
      const std::array<unsigned char,
                       crypto_aead_xchacha20poly1305_ietf_KEYBYTES> *key =
          nullptr);

  // Compression methods
  std::vector<std::byte>
  compress_data(const std::vector<std::byte> &plaintext_data);
  std::vector<std::byte>
  decompress_data(const std::vector<std::byte> &compressed_data,
                  size_t original_size);

  // Encryption methods
  // Note: Using std::vector<unsigned char> for nonce as typically nonces are
  // raw byte buffers. And std::array<unsigned char, KEYBYTES> for key for fixed
  // size.
  std::vector<std::byte> encrypt_data(
      const std::vector<std::byte> &plaintext_data,
      const std::array<unsigned char,
                       crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &key,
      std::vector<unsigned char> &nonce_output);
  std::vector<std::byte> decrypt_data(
      const std::vector<std::byte> &ciphertext_data,
      const std::array<unsigned char,
                       crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &key,
      const std::vector<unsigned char> &nonce);

private:
  std::vector<std::byte> buffer_;
  crypto_hash_sha256_state hash_state_; // Libsodium SHA-256 state
  bool finalized_ = false;    // Tracks if finalize_hashed() has been called
  int compression_level_ = 1; ///< Zstd compression level
  CipherAlgorithm cipher_algo_ =
      CipherAlgorithm::XCHACHA20_POLY1305; ///< Selected encryption algorithm
  bool hashing_enabled_ = false;
  bool compression_enabled_ = false;
  bool encryption_enabled_ = false;
};

#endif // BLOCKIO_HPP
