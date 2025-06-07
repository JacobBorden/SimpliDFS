#ifndef KEY_MANAGER_HPP
#define KEY_MANAGER_HPP

#include <array>
#include <mutex>
#include <sodium.h>
#include <string>
#include <chrono>

namespace simplidfs {

class KeyManager {
public:
    static KeyManager& getInstance();

    // Initializes the key manager. Reads key from the environment or generates a new one.
    void initialize();

    // Copies the cluster-wide key into the provided array.
    void getClusterKey(std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const;

    /**
     * Rotates the cluster encryption key.
     * The previous key remains available via getPreviousClusterKey() for
     * the specified window in seconds.
     */
    void rotateClusterKey(unsigned int windowSeconds);

    /**
     * Retrieves the previous cluster key if it is still valid.
     * @return true if a previous key was returned, false otherwise.
     */
    bool getPreviousClusterKey(std::array<unsigned char, crypto_aead_aes256gcm_KEYBYTES>& out) const;

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
    void purgeExpiredOldKey() const;

    unsigned char* key_;
    mutable unsigned char* old_key_;
    mutable std::chrono::steady_clock::time_point old_key_expiration_;
    bool initialized_;
    mutable std::mutex mutex_;
};

} // namespace simplidfs

#endif // KEY_MANAGER_HPP
