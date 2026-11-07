#include "utilities/key_manager.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace {
void *live[32]{};
int allocations = 0, frees = 0, checks = 0;
bool failNext = false;
void check(bool ok, const char *label) {
  ++checks;
  if (!ok)
    throw std::runtime_error(label);
}
void finalCheck() {
  if (allocations != frees) {
    std::fprintf(stderr, "secure allocation leak: %d allocated %d freed\n",
                 allocations, frees);
    std::_Exit(91);
  }
  std::printf("cleanup: %d allocations freed and zeroed\n", frees);
}
using Key =
    std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>;
} // namespace
extern "C" void *__real_sodium_malloc(size_t);
extern "C" void __real_sodium_free(void *);
extern "C" void *__wrap_sodium_malloc(size_t size) {
  if (failNext) {
    failNext = false;
    return nullptr;
  }
  void *p = __real_sodium_malloc(size);
  if (p) {
    if (allocations >= 32)
      std::abort();
    live[allocations++] = p;
  }
  return p;
}
extern "C" void __wrap_sodium_free(void *p) {
  if (p) {
    bool found = false;
    for (auto &v : live)
      if (v == p) {
        v = nullptr;
        found = true;
        break;
      }
    if (!found)
      std::abort();
    for (size_t i = 0; i < Key{}.size(); ++i)
      if (static_cast<unsigned char *>(p)[i] != 0)
        std::abort();
    ++frees;
  }
  __real_sodium_free(p);
}
int main(int argc, char **argv) {
  std::atexit(finalCheck); // Registered before singleton destruction.
  try {
    check(argc == 2, "mode required");
    check(setenv("SIMPLIDFS_CLUSTER_KEY", std::string(64, '1').c_str(), 1) == 0,
          "environment");
    auto &km = simplidfs::KeyManager::getInstance();
    const bool initFailure =
        std::strcmp(argv[1], "initialization-failure") == 0;
    if (initFailure) {
      failNext = true;
      bool threw = false;
      try {
        km.initialize();
      } catch (const std::runtime_error &) {
        threw = true;
      }
      check(threw, "initialization failure reported");
      Key output{};
      threw = false;
      try {
        km.getClusterKey(output);
      } catch (const std::runtime_error &) {
        threw = true;
      }
      check(threw, "failed initialization remains uninitialized");
    }
    km.initialize();
    Key original{}, before{}, previous{}, after{};
    km.getClusterKey(original);
    check(original[0] == 0x11 && original.back() == 0x11, "provisioned key");
    const bool withPrevious =
        std::strcmp(argv[1], "failure-with-previous") == 0;
    const bool noPrevious =
        std::strcmp(argv[1], "failure-without-previous") == 0;
    if (withPrevious) {
      km.rotateClusterKey(3600);
      check(km.getPreviousClusterKey(previous) && previous == original,
            "initial previous key");
    }
    km.getClusterKey(before);
    if (withPrevious || noPrevious) {
      const int oldFrees = frees;
      failNext = true;
      bool threw = false;
      try {
        km.rotateClusterKey(0);
      } catch (const std::runtime_error &e) {
        threw =
            std::strcmp(
                e.what(),
                "Unable to allocate secure memory for new encryption key") == 0;
      }
      check(threw, "allocation error contract");
      check(frees == oldFrees, "failed rotation must not discard previous key");
      // Also checks the original implementation's null-current-key defect.
      km.getClusterKey(after);
      check(after == before, "failed rotation preserves current key");
      if (withPrevious)
        check(km.getPreviousClusterKey(after) && after == original,
              "previous key and window preserved");
      else
        check(!km.getPreviousClusterKey(after),
              "failure creates no previous key");
    }
    for (int i = 0; i < 3; ++i) {
      km.getClusterKey(before);
      km.rotateClusterKey(3600);
      km.getClusterKey(after);
      check(after != before, "rotation replaces current key");
      check(km.getPreviousClusterKey(previous) && previous == before,
            "rotation retains most recent key");
    }
    km.rotateClusterKey(0);
    check(!km.getPreviousClusterKey(previous),
          "zero window expires previous key");
    check(!km.getPreviousClusterKey(previous), "repeated purge safe");
    km.getClusterKey(before);
    km.getUserKey("disposable-user", after);
    check(after == before, "existing user alias unchanged");
    km.getVolumeKey("disposable-volume", after);
    check(after == before, "existing volume alias unchanged");
    km.initialize();
    km.getClusterKey(after);
    check(after == before, "reinitialization idempotent");
    std::printf("PASS %s: %d checks\n", argv[1], checks);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL: %s (%d checks reached)\n", e.what(), checks);
    return 1;
  }
}
