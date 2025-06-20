#include "utilities/key_manager.hpp"
#include "gtest/gtest.h"
#include <array>
#include <chrono>
#include <string>
#include <thread>

TEST(KeyManagerTest, ReturnsConsistentKey) {
  simplidfs::KeyManager &km = simplidfs::KeyManager::getInstance();
  km.initialize();
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key1;
  km.getClusterKey(key1);
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> key2;
  km.getClusterKey(key2);
  EXPECT_EQ(key1, key2);
}

TEST(KeyManagerTest, RotationPreservesOldKey) {
  using namespace std::chrono_literals;
  simplidfs::KeyManager &km = simplidfs::KeyManager::getInstance();
  km.initialize();
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
      original;
  km.getClusterKey(original);
  km.rotateClusterKey(1); // 1 second window
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>
      current;
  km.getClusterKey(current);
  EXPECT_NE(original, current);
  std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES> prev;
  EXPECT_TRUE(km.getPreviousClusterKey(prev));
  EXPECT_EQ(prev, original);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_FALSE(km.getPreviousClusterKey(prev));
}
