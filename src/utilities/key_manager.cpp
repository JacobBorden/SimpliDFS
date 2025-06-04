#include "utilities/key_manager.hpp"
#include <cstdlib>  // for getenv
#include <cstring>
#include <stdexcept>
#include <sodium.h>

namespace simplidfs {

KeyManager& KeyManager::getInstance() {
    static KeyManager instance;
    return instance;
}

KeyManager::KeyManager() : key_(nullptr), initialized_(false) {}

KeyManager::~KeyManager() {
    if (key_) {
        sodium_memzero(key_, crypto_aead_aes256gcm_KEYBYTES);
        sodium_free(key_);
        key_ = nullptr;
    }
}

void KeyManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return;
    }

    if (sodium_init() < 0) {
        throw std::runtime_error("Failed to initialize libsodium");
    }

    key_ = static_cast<unsigned char*>(sodium_malloc(crypto_aead_aes256gcm_KEYBYTES));
    if (!key_) {
        throw std::runtime_error("Unable to allocate secure memory for encryption key");
    }

    if (!loadKeyFromEnv()) {
        generateClusterKey();
    }

    initialized_ = true;
}

bool KeyManager::loadKeyFromEnv() {
    const char* env_key = std::getenv("SIMPLIDFS_CLUSTER_KEY");
    if (!env_key) {
        return false;
    }
    std::string hex(env_key);
    if (hex.size() != crypto_aead_aes256gcm_KEYBYTES * 2) {
        return false;
    }
    for (size_t i = 0; i < crypto_aead_aes256gcm_KEYBYTES; ++i) {
        std::string byte_str = hex.substr(i * 2, 2);
        try {
            unsigned long byte_val = std::stoul(byte_str, nullptr, 16);
            key_[i] = static_cast<unsigned char>(byte_val);
        } catch (...) {
            return false;
        }
    }
    return true;
}

void KeyManager::generateClusterKey() {
    randombytes_buf(key_, crypto_aead_aes256gcm_KEYBYTES);
}

void KeyManager::getClusterKey(std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        throw std::runtime_error("KeyManager not initialized");
    }
    std::memcpy(out.data(), key_, crypto_aead_aes256gcm_KEYBYTES);
}

void KeyManager::getUserKey(const std::string& /*userId*/,
                            std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const {
    // Future implementation will provide per-user keys
    getClusterKey(out);
}

void KeyManager::getVolumeKey(const std::string& /*volumeId*/,
                              std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const {
    // Future implementation will provide per-volume keys
    getClusterKey(out);
}

} // namespace simplidfs

