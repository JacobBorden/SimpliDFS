#ifndef KEY_MANAGER_HPP
#define KEY_MANAGER_HPP

#include <array>
#include <mutex>
#include <sodium.h>
#include <string>

namespace simplidfs {

class KeyManager {
public:
    static KeyManager& getInstance();

    // Initializes the key manager. Reads key from the environment or generates a new one.
    void initialize();

    // Copies the cluster-wide key into the provided array.
    void getClusterKey(std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const;

    // Placeholders for future per-user and per-volume keys.
    void getUserKey(const std::string& userId,
                    std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const;
    void getVolumeKey(const std::string& volumeId,
                      std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const;

private:
    KeyManager();
    ~KeyManager();
    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;

    bool loadKeyFromEnv();
    void generateClusterKey();

    unsigned char* key_;
    bool initialized_;
    mutable std::mutex mutex_;
};

} // namespace simplidfs

#endif // KEY_MANAGER_HPP
