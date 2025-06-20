#include "utilities/fips.h"
#include "blake3.h"
#include <cstring>
#include <sodium.h>

bool fips_self_test() {
  if (sodium_init() < 0) {
    return false;
  }
  const char msg[] = "The quick brown fox jumps over the lazy dog";
  unsigned char digest[BLAKE3_OUT_LEN];
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, reinterpret_cast<const uint8_t *>(msg),
                       sizeof(msg) - 1);
  blake3_hasher_finalize(&hasher, digest, BLAKE3_OUT_LEN);
  const unsigned char expected[BLAKE3_OUT_LEN] = {
      0x2f, 0x15, 0x14, 0x18, 0x1a, 0xad, 0xcc, 0xd9, 0x13, 0xab, 0xd9,
      0x4c, 0xfa, 0x59, 0x27, 0x01, 0xa5, 0x68, 0x6a, 0xb2, 0x3f, 0x8d,
      0xf1, 0xdf, 0xf1, 0xb7, 0x47, 0x10, 0xfe, 0xbc, 0x6d, 0x4a};
  return std::memcmp(digest, expected, BLAKE3_OUT_LEN) == 0;
}
