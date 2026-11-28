// SPDX-License-Identifier: MIT
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>

#include <array>
#include <cstring>
#include <iostream>
#include <memory>

int main(int argc, char **argv) {
  if (argc > 2 || (argc == 2 && std::strcmp(argv[1], "--wrong-digest") != 0))
    return 2;
  const bool negative = argc == 2;
  using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  using TlsContext = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
  DigestContext digest(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  TlsContext tls(SSL_CTX_new(TLS_method()), SSL_CTX_free);
  if (!digest || !tls ||
      SSL_CTX_set_min_proto_version(tls.get(), TLS1_2_VERSION) != 1)
    return 3;
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int size = 0;
  if (EVP_DigestInit_ex(digest.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(digest.get(), "abc", 3) != 1 ||
      EVP_DigestFinal_ex(digest.get(), bytes.data(), &size) != 1)
    return 4;
  std::array<unsigned char, 32> expected{
      0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
      0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
      0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
  if (negative)
    expected[0] ^= 1;
  if (size != expected.size() ||
      CRYPTO_memcmp(bytes.data(), expected.data(), expected.size()) != 0)
    return 1;
  std::cout << "headers=" << OPENSSL_VERSION_TEXT
            << " runtime=" << OpenSSL_version(OPENSSL_VERSION) << '\n';
  return 0;
}
