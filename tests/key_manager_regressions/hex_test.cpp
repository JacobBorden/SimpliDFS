#include "utilities/key_manager.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
using Key =
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>;
void *live = nullptr;
int allocations = 0, frees = 0, random_calls = 0, checks = 0;
void check(bool ok, const char *label) {
  ++checks;
  if (!ok)
    throw std::runtime_error(label);
}
void finalCheck() {
  if (live || allocations != frees) {
    std::fprintf(stderr, "secure allocation leak\n");
    std::_Exit(91);
  }
  std::printf("cleanup: %d allocation freed and zeroed\n", frees);
}
} // namespace
extern "C" void *__real_sodium_malloc(size_t);
extern "C" void __real_sodium_free(void *);
extern "C" void *__wrap_sodium_malloc(size_t size) {
  if (live || size != Key{}.size())
    std::abort();
  live = __real_sodium_malloc(size);
  if (live)
    ++allocations;
  return live;
}
extern "C" void __wrap_sodium_free(void *p) {
  if (p) {
    if (p != live)
      std::abort();
    for (size_t i = 0; i < Key{}.size(); ++i)
      if (static_cast<unsigned char *>(p)[i] != 0)
        std::abort();
    live = nullptr;
    ++frees;
  }
  __real_sodium_free(p);
}
extern "C" void __wrap_randombytes_buf(void *p, size_t size) {
  if (p != live || size != Key{}.size())
    std::abort();
  ++random_calls;
  std::memset(p, 0xa5, size);
}
int main(int argc, char **argv) {
  std::atexit(finalCheck);
  try {
    check(argc == 4, "input, expected bytes, and validity required");
    const bool missing = std::strcmp(argv[1], "MISSING") == 0;
    check((missing ? unsetenv("SIMPLIDFS_CLUSTER_KEY")
                   : setenv("SIMPLIDFS_CLUSTER_KEY", argv[1], 1)) == 0,
          "environment setup");
    auto &km = simplidfs::KeyManager::getInstance();
    Key key{}, expected{}, previous{};
    bool threw = false;
    try {
      km.getClusterKey(key);
    } catch (const std::runtime_error &e) {
      threw = std::strcmp(e.what(), "KeyManager not initialized") == 0;
    }
    check(threw, "uninitialized getter contract");
    check(std::strlen(argv[2]) == expected.size() * 2, "expected length");
    for (size_t i = 0; i < expected.size(); ++i) {
      unsigned int v = 0;
      check(std::sscanf(argv[2] + i * 2, "%2x", &v) == 1, "expected byte");
      expected[i] = static_cast<unsigned char>(v);
    }
    km.initialize();
    const bool valid = std::strcmp(argv[3], "valid") == 0;
    check(random_calls == (valid ? 0 : 1),
          "strict parser must use fallback exactly for invalid configuration");
    km.getClusterKey(key);
    check(key == expected, "all configured or fallback bytes match");
    check(!km.getPreviousClusterKey(previous), "initial key has no previous");
    km.getUserKey("disposable", previous);
    check(previous == key, "existing user alias contract");
    km.getVolumeKey("disposable", previous);
    check(previous == key, "existing volume alias contract");
    check(setenv("SIMPLIDFS_CLUSTER_KEY", "invalid-after-init", 1) == 0,
          "idempotence input");
    km.initialize();
    km.getClusterKey(previous);
    check(previous == key, "initialization remains idempotent");
    check(random_calls == (valid ? 0 : 1), "idempotence does not regenerate");
    check(allocations == 1 && frees == 0, "one live secure key");
    std::printf("PASS: %d checks\n", checks);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL: %s (%d checks reached)\n", e.what(), checks);
    return 1;
  }
}
