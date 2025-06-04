#include "gtest/gtest.h"
#include "utilities/key_manager.hpp"
#include <array>
#include <string>

TEST(KeyManagerTest, ReturnsConsistentKey) {
    simplidfs::KeyManager& km = simplidfs::KeyManager::getInstance();
    km.initialize();
    std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key1;
    km.getClusterKey(key1);
    std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES> key2;
    km.getClusterKey(key2);
    EXPECT_EQ(key1, key2);
}
