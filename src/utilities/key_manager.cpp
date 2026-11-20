#include "utilities/key_manager.hpp"
#include <chrono>
#include <cstdlib> // for getenv
#include <cstring>
#include <memory>
#include <sodium.h>
#include <stdexcept>

namespace simplidfs {

KeyManager &KeyManager::getInstance() {
  static KeyManager instance;
  return instance;
}

KeyManager::KeyManager()
    : key_(nullptr), old_key_(nullptr),
      old_key_expiration_(std::chrono::steady_clock::time_point::min()),
      initialized_(false) {}

KeyManager::~KeyManager() {
  if (key_) {
    sodium_memzero(key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    sodium_free(key_);
    key_ = nullptr;
  }
  if (old_key_) {
    sodium_memzero(old_key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    sodium_free(old_key_);
    old_key_ = nullptr;
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

  key_ = static_cast<unsigned char *>(
      sodium_malloc(crypto_aead_xchacha20poly1305_ietf_KEYBYTES));
  if (!key_) {
    throw std::runtime_error(
        "Unable to allocate secure memory for encryption key");
  }

  if (!loadKeyFromEnv()) {
    generateClusterKey();
  }

  initialized_ = true;
}

bool KeyManager::loadKeyFromEnv() {
  const char *env_key = std::getenv("SIMPLIDFS_CLUSTER_KEY");
  if (!env_key) {
    return false;
  }
  std::string hex(env_key);
  if (hex.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES * 2) {
    return false;
  }
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
      decoded{};
  size_t decoded_size = 0;
  const char *hex_end = nullptr;
  if (sodium_hex2bin(decoded.data(), decoded.size(), hex.data(), hex.size(),
                     nullptr, &decoded_size, &hex_end) != 0 ||
      decoded_size != decoded.size() || hex_end != hex.data() + hex.size()) {
    sodium_memzero(decoded.data(), decoded.size());
    return false;
  }
  std::memcpy(key_, decoded.data(), decoded.size());
  sodium_memzero(decoded.data(), decoded.size());
  return true;
}

void KeyManager::generateClusterKey() {
  randombytes_buf(key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
}

void KeyManager::getClusterKey(
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &out)
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    throw std::runtime_error("KeyManager not initialized");
  }
  std::memcpy(out.data(), key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
}

void KeyManager::getUserKey(
    const std::string & /*userId*/,
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &out)
    const {
  // Future implementation will provide per-user keys
  getClusterKey(out);
}

void KeyManager::getVolumeKey(
    const std::string & /*volumeId*/,
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &out)
    const {
  // Future implementation will provide per-volume keys
  getClusterKey(out);
}

void KeyManager::purgeExpiredOldKey() const {
  if (old_key_ && std::chrono::steady_clock::now() >= old_key_expiration_) {
    sodium_memzero(old_key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    sodium_free(old_key_);
    old_key_ = nullptr;
  }
}

void KeyManager::rotateClusterKey(unsigned int windowSeconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    throw std::runtime_error("KeyManager not initialized");
  }
  // Prepare the replacement before changing either owned key or its window.
  const auto freeKey = [](unsigned char *key) noexcept {
    sodium_memzero(key, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    sodium_free(key);
  };
  std::unique_ptr<unsigned char, decltype(freeKey)> replacement(
      static_cast<unsigned char *>(
          sodium_malloc(crypto_aead_xchacha20poly1305_ietf_KEYBYTES)),
      freeKey);
  if (!replacement) {
    throw std::runtime_error(
        "Unable to allocate secure memory for new encryption key");
  }
  randombytes_buf(replacement.get(),
                  crypto_aead_xchacha20poly1305_ietf_KEYBYTES);

  purgeExpiredOldKey();
  if (old_key_) {
    sodium_memzero(old_key_, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    sodium_free(old_key_);
  }
  old_key_ = key_;
  key_ = replacement.release();
  old_key_expiration_ =
      std::chrono::steady_clock::now() + std::chrono::seconds(windowSeconds);
}

bool KeyManager::getPreviousClusterKey(
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> &out)
    const {
  std::lock_guard<std::mutex> lock(mutex_);
  purgeExpiredOldKey();
  if (!old_key_) {
    return false;
  }
  std::memcpy(out.data(), old_key_,
              crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
  return true;
}

} // namespace simplidfs
