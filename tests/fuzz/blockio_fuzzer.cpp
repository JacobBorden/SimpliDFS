#include "utilities/blockio.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <string> // Required by blockio.hpp for DigestResult::cid
#include "utilities/logger.h"

// Helper function to create a key from fuzzer data
std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> getKey(const uint8_t* data, size_t size) {
    std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key;
    size_t key_material_size = std::min(size, key.size());
    for (size_t i = 0; i < key_material_size; ++i) {
        key[i] = data[i];
    }
    for (size_t i = key_material_size; i < key.size(); ++i) {
        key[i] = 0; // Pad with zeros if not enough data
    }
    return key;
}

// Helper function to create a nonce from fuzzer data
std::vector<unsigned char> getNonce(const uint8_t* data, size_t size) {
    if (size < crypto_aead_aes256gcm_NPUBBYTES) { // crypto_aead_aes256gcm_NPUBBYTES is the typical nonce size
        // Return a default or minimal valid nonce if data is too short
        return std::vector<unsigned char>(crypto_aead_aes256gcm_NPUBBYTES, 0);
    }
    return std::vector<unsigned char>(data, data + crypto_aead_aes256gcm_NPUBBYTES);
}


extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    Logger::getInstance().initialize("fuzzer_run.log", LogLevel::ERROR);
    BlockIO bio;

    // Use a portion of data for ingest
    if (Size > 0) {
        bio.ingest(reinterpret_cast<const std::byte*>(Data), Size);
    }

    // Test finalize_raw and finalize_hashed
    // It's okay if these are called multiple times or on empty buffer by fuzzer
    std::vector<std::byte> raw_data = bio.finalize_raw();
    // Re-initialize BlockIO for hashed operations as finalize_raw might alter state expectations
    BlockIO bio_hash;
    if (Size > 0) {
        bio_hash.ingest(reinterpret_cast<const std::byte*>(Data), Size);
    }
    DigestResult hash_result = bio_hash.finalize_hashed();

    // Test compression/decompression
    if (raw_data.size() > 0) { // Use raw_data from finalize_raw
        std::vector<std::byte> compressed = bio.compress_data(raw_data);
        if (!compressed.empty()) {
            // For decompression, we need the original size.
            // This is a tricky part for fuzzing without more context or modifying decompress_data.
            // For now, let's assume successful compression implies decompress_data can be called.
            // A more robust fuzzer might try to guess original_size or store it.
            // We use raw_data.size() as the original_size which should be correct here.
            try {
                 std::vector<std::byte> decompressed = bio.decompress_data(compressed, raw_data.size());
                 // Optional: Add assertion if (decompressed != raw_data) { /* error */ }
            } catch (const std::exception& e) {
                // Catch potential exceptions from decompress_data (e.g., if compressed data is corrupt)
            }
        }
    }

    // Test encryption/decryption
    // Use a slice of Data for key and nonce material, if Size is large enough
    if (Size > (crypto_aead_aes256gcm_KEYBYTES + crypto_aead_aes256gcm_NPUBBYTES)) {
        std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key = getKey(Data, crypto_aead_aes256gcm_KEYBYTES);
        // Use next part of Data for nonce
        std::vector<unsigned char> nonce_material(Data + crypto_aead_aes256gcm_KEYBYTES, Data + crypto_aead_aes256gcm_KEYBYTES + crypto_aead_aes256gcm_NPUBBYTES);

        std::vector<unsigned char> nonce_encrypt_output; // Will be populated by encrypt_data

        std::vector<std::byte> plaintext_to_encrypt;
        if (!raw_data.empty()) {
            plaintext_to_encrypt = raw_data;
        } else if (Size > (crypto_aead_aes256gcm_KEYBYTES + crypto_aead_aes256gcm_NPUBBYTES)) {
            // If raw_data is empty (e.g. Size was 0 for ingest), use remaining part of Data as plaintext
            const uint8_t* plaintext_ptr = Data + crypto_aead_aes256gcm_KEYBYTES + crypto_aead_aes256gcm_NPUBBYTES;
            size_t plaintext_size = Size - (crypto_aead_aes256gcm_KEYBYTES + crypto_aead_aes256gcm_NPUBBYTES);
            if (plaintext_size > 0) {
                 plaintext_to_encrypt.assign(reinterpret_cast<const std::byte*>(plaintext_ptr),
                                             reinterpret_cast<const std::byte*>(plaintext_ptr) + plaintext_size);
            }
        }


        if (!plaintext_to_encrypt.empty()) {
            std::vector<std::byte> encrypted = bio.encrypt_data(plaintext_to_encrypt, key, nonce_encrypt_output);
            if (!encrypted.empty() && !nonce_encrypt_output.empty()) {
                try {
                    std::vector<std::byte> decrypted = bio.decrypt_data(encrypted, key, nonce_encrypt_output);
                    // Optional: Add assertion if (decrypted != plaintext_to_encrypt) { /* error */ }
                } catch (const std::exception& e) {
                    // Catch potential exceptions from decrypt_data
                }
            }
        }
    }

    return 0; // Non-zero return values are reserved for future use.
}
