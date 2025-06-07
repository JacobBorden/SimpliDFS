#include "utilities/fips.h"
#include <sodium.h>
#include <cstring>

bool fips_self_test() {
    if (sodium_init() < 0) {
        return false;
    }
    const char msg[] = "The quick brown fox jumps over the lazy dog";
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest,
                       reinterpret_cast<const unsigned char*>(msg),
                       sizeof(msg) - 1);
    const unsigned char expected[crypto_hash_sha256_BYTES] = {
        0xd7,0xa8,0xfb,0xb3,0x07,0xd7,0x80,0x94,
        0x69,0xca,0x9a,0xbc,0xb0,0x08,0x2e,0x4f,
        0x8d,0x56,0x51,0xe4,0x6d,0x3c,0xdb,0x76,
        0x2d,0x02,0xd0,0xbf,0x37,0xc9,0xe5,0x92
    };
    return std::memcmp(digest, expected, crypto_hash_sha256_BYTES) == 0;
}
