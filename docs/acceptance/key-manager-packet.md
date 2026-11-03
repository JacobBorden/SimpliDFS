# Key manager acceptance packet

This packet qualifies the strict hexadecimal parser and rotation allocation rollback on the existing draft PR #260. It does not qualify persistent key provisioning or distributed storage. Canonical `origin/development`, fetched for this run, is `02aaa408e77b98a252d26e33c5f2973f87b47c25`; the tested task source is `dabc9b3874f60d79a3489b631d9a85c074bfbbf5`, which already contains that base. Follow-up packet commits change documentation only. No older checkout scripts are used.

## Repeatable focused validation

Run from the persistent task worktree with a fresh disposable build directory. Linux GNU-compatible linker wrapping is required. The parent project retains C++17; this packet does not change the standard requirement.

```bash
mkdir -p work/key-validation
for suite in hex rotation combined; do
  wraps=(-Wl,--wrap=sodium_malloc -Wl,--wrap=sodium_free)
  if [ "$suite" != rotation ]; then wraps+=(-Wl,--wrap=randombytes_buf); fi
  c++ -std=c++17 -g -O0 -pthread -Iinclude \
    "tests/key_manager_regressions/${suite}_test.cpp" \
    src/utilities/key_manager.cpp -lsodium "${wraps[@]}" \
    -o "work/key-validation/$suite" || exit 1
  python3 -B tests/key_manager_regressions/run_regressions.py \
    --suite "$suite" --executable "work/key-validation/$suite" || exit 1
done
```

Fresh local results: three builds exit 0; hex 55/55, rotation 4/4 and combined 55/55 independent processes pass, zero unexpected stderr or failures. Every process reports secure allocations freed and zeroed. Toolchain: SUSE GCC 15.2.1 (20251006), system libsodium 1.0.20, CMake 3.31.0. This direct component check uses shared system sodium, not the project's fetched static 1.0.19 or a full-project build. Exact source SHA256 bindings and command/output receipts are in [key-manager-validation.json](key-manager-validation.json). Fixed keys and deterministic randomness are synthetic test fixtures, not production provisioning or entropy evidence.

## Inventory and remote evidence

| Check | Scope and observed result at tested source |
| --- | --- |
| KeyManagerRegression.hex | 55 configured valid/invalid/missing-input cases, fresh process per case |
| KeyManagerRegression.rotation | Four allocation/initialization/success modes |
| KeyManagerRegression.combined | 55 parser cases combined with rotation modes |
| CMake registration | Three CTest entries on Linux GNU/Clang; absent on other platforms |
| Full CMake Release CI | [Run 41362070530](https://github.com/JacobBorden/SimpliDFS/actions/runs/41362070530) success at dabc9b3; PR records 145 executed tests and two inherited disabled tests |
| Fuzz / Raft CI | [Fuzz](https://github.com/JacobBorden/SimpliDFS/actions/runs/41362070544) and [Raft](https://github.com/JacobBorden/SimpliDFS/actions/runs/41362070524) successful workflow conclusions; not full acceptance |
| FUSE stress | [Run 41362070521](https://github.com/JacobBorden/SimpliDFS/actions/runs/41362070521) failed; downloaded log confirms append verification expected 200 lines, got 1 |

Full-project reproduction uses `cmake -S . -B work/full-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_POSIX_TEST_SUITE=ON -DBUILD_SHARED_LIBS=OFF -DWITH_STATIC_LIBS=ON`, followed by `cmake --build work/full-build` and the workflow's `ctest --test-dir work/full-build -C Release --output-on-failure -VV -E FuseTestEnv --timeout 1200`. These are required gates, not replaced by the focused commands above. Run FUSE only with reviewed cleanup and disposable fixtures.

Fresh full-project configure with the command above failed at `CMakeLists.txt:52`: default `/lib64/cmake/Boost-1.86.0` discovery rejected shared `libboost_system.so.1.86.0` because `Boost_USE_STATIC_LIBS=ON`. No full local build or CTest run followed. This is a discovery failure, not evidence that the previously installed static package is absent. Earlier automation memory records a qualified static package and initial cache at canonical repository `work/simplidfs-boost-install-20261011-manual-01/boost-cache.cmake`; a later full-build continuation should verify and use that cache with this task worktree, then qualify the remaining dependencies. No dependency requirements were weakened.

## Acceptance limits and next step

Malformed configuration still selects a random process key; user and volume getters still alias the cluster key. Rotation tests qualify allocation rollback and cleanup, not durable distribution or retained-key backup. Acknowledged-write, restart/restore, key-loss, authentication/isolation, FUSE, crash/fsync and disposable three-node gates remain open. Prior isolated restore evidence in the Space plan is historical component evidence and is not automatically transferred to this PR.

Keep #260 draft. Next useful step is a scoped reproduction of the failed append verification retaining node diagnostics and returned bytes, with a baseline comparison before attributing causality. No public service, host filesystem replacement or real data is authorized.

Merge checklist: formatting unchanged; full CI is not green (FUSE failed); version/changelog already paired at 0.11.53 for this draft; coverage diff unmeasured; reviewer approval remains required. This packet is review evidence, not merge acceptance.

## November 2 static Boost continuation

The same persistent branch was validated against freshly fetched canonical base
`02aaa408e77b98a252d26e33c5f2973f87b47c25`, task parent
`1065fb24e828899e02ef16919b4fb19bbcf9502c`. The existing static Boost 1.86.0
archive matches SHA256 `a56005627c3836648852bde071f3bdb85d9ec1e3f41df81cb4c23f6c3142ef05`.
The initial-cache argument alone is insufficient in a build directory retaining
`boost_system_DIR=/lib64/cmake/boost_system-1.86.0`: CMake continues to select
that cached shared-only component. Clear stale component discovery entries when
resuming; no dependency reinstall or requirement change is needed.

From the persistent task worktree, set `boost_cache` to the previously recorded
absolute `boost-cache.cmake` path, then run:

```bash
cmake -S docs/acceptance/static-boost-probe -B work/static-build-packet/published-probe -G Ninja -C "$boost_cache"
cmake --build work/static-build-packet/published-probe
ctest --test-dir work/static-build-packet/published-probe --output-on-failure
cmake -S . -B work/daily-acceptance/full-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_POSIX_TEST_SUITE=ON \
  -DBUILD_SHARED_LIBS=OFF -DWITH_STATIC_LIBS=ON \
  -C "$boost_cache" -U 'boost_*_DIR'
```

The committed standalone probe requires exact Boost 1.86.0, asserts that the
imported Release target resolves to an `.a`, links it and checks default-success,
permission-denied and nonempty-message behavior. Configure/build/CTest pass
(1/1 dependency test); `ldd` has no shared Boost dependency. GCC 15.2.1 and
CMake 3.31.0 were used. This dependency probe is not a SimpliDFS runtime test or
a fully static executable; libc, libstdc++ and libm remain dynamic in the probe.

Full configure with cleared component entries exited 1 at `CMakeLists.txt:122`:
FetchContent's yaml-cpp clone failed three times with `fatal: write error:
Input/output error` and `fatal: fetch-pack: invalid index-pack output`. The
failing destination is `work/daily-acceptance/full-build/_deps/yaml-cpp-src`
inside this persistent worktree. No permission changes or repository substitution
were attempted. No full-project build or runtime tests followed. Discovery also
reported gRPC 1.73.0 with Protobuf 3.21.12 (`/usr/lib/libprotobuf.a`); matching
compiler/header/runtime qualification remains required, as in the historical
baseline adapters. These observations do not establish compatible static linking.

Resume this same branch by resolving the yaml-cpp fetch I/O failure, rerun the
recorded configure, qualify matched dependency versions, then build and run the
required tests. Existing FUSE failure and all durability gates remain open.
The prior head's CMake/Fuzz/Raft success and FUSE failure were reread; subsequent
packet-head CI must be checked separately. Machine-readable hashes and captured
failure output are in `static-boost-validation.json`.
