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

## November 3 protocol dependency checkpoint

Canonical development fetched again remains
`02aaa408e77b98a252d26e33c5f2973f87b47c25`; task parent is
`83a8e3c9a7c0d0462d29493ea569f2a4755c1bc2`. This bounded continuation adds
`protobuf-probe/run.py` and `main.cpp`; production source and dependencies are
unchanged. Acceptance for this increment is a reproducible generation, syntax,
link and disposable round-trip checkpoint with runtime-library evidence and
explicit qualification limits. It does not accept the full dependency stack.

From this task worktree, choose a fresh output path and run:

```bash
python3 docs/acceptance/protobuf-probe/run.py --output work/protobuf-packet/final
```

GCC 15.2.1 generated the actual `proto/filesystem.proto` using protoc 3.21.12
and the installed gRPC plugin. Both generated translation units passed C++17
syntax checks. The probe linked the same `/usr/lib/libprotobuf.a` selected by
project discovery, plus pkg-config's gRPC libraries, and passed one process
checking serialization/parsing with an embedded NUL, field preservation and
client-stub construction. No RPC is sent or service started. This is one
component runtime execution, not a SimpliDFS suite or distributed test.

The recorded toolchain is mixed: pkg-config advertises Protobuf 31.1.0, whereas
protoc/default headers and the static archive are 3.21.12. Runtime `ldd` shows
gRPC 1.73 and both Abseil 2501 and 2505 families; no dynamic Protobuf is loaded.
A successful small round trip does not qualify that mixed Abseil closure or
full-static compatibility. Preserve the earlier matched-version adapter evidence;
a later full-build continuation must deliberately select and verify one matched
compiler/header/runtime closure. This probe intentionally records the current
discovery without changing system libraries or project requirements.

All command statuses, complete outputs and source hashes are recorded in
`protobuf-validation.json`. The previous parent CI has CMake/Fuzz/Raft success
and FUSE failure; the steward reports CMake/FUSE used synthetic merge
`424042fa9206f77aeb9883bed04f1ba12a4a6290`. Those results are not acceptance
of this new commit. The prior yaml-cpp fetch I/O failure is retained. This run
observed only 1.1 GB free on C:; it did not retry a large fetch, delete files or
attribute the earlier I/O error conclusively to capacity. Full-project runtime
executions this run: zero. Existing disabled-test, FUSE, provisioning, durability,
recovery and three-node gates stay open. Keep PR260 draft. Next: resolve the
fetch/storage prerequisite and select a matched dependency closure before the
full configure/build/CTest gates.

## November 4 configure checkpoint (November 5 UTC)

This continues the same unfinished build packet on the persistent task branch.
Fresh canonical development is `02aaa408e77b98a252d26e33c5f2973f87b47c25`;
tested task parent is `d1f3bb5b15701c59b93955d6169b7693ea348151`.
The parent contains the canonical base; no integration conflict or branch rewrite
was needed. Acceptance for this increment is a repeatable configure checkpoint
with exact input hashes, raw output and explicit dependency gaps.

Run the full configure command from the November 2 subsection, in this task
worktree, with the same static Boost cache and cleared `boost_*_DIR` entries.
The retry now passes the yaml-cpp population point and reaches utility-library
configuration. The prior I/O failure remains historical evidence; this result
neither identifies its cause nor qualifies yaml-cpp compilation.

Configuration fails at `src/utilities/CMakeLists.txt:5`: FindOpenSSL reports
`missing: OPENSSL_CRYPTO_LIBRARY`, with headers reporting version 3.5.3.
The utility library explicitly sets `OPENSSL_USE_STATIC_LIBS ON`. Neither
libcrypto.a nor libssl.a exists at the inspected `/usr/lib` and `/usr/lib64`
paths. pkg-config finds shared libraries and its static flags additionally name
`libjitterentropy.so`; those flags do not prove a usable static dependency
closure. No shared-library substitution or requirement change was made.
GCC 15.2.1 / CMake 3.31.0 were used. Complete command exits, raw configure
outputs, fetched yaml-cpp revision, source hashes and standard-path archive
observations are in `configure-validation.json`. Zero full-project builds or
runtime tests ran because generation did not complete.

Parent-head workflow metadata reports CMake, Fuzz and Raft success, FUSE stress
failure; those statuses are not validation of this documentation head. The fuzz
workflow's failure suppression remains a qualification limit. Keep PR260 draft.
Next continue this same task by providing and qualifying the required static
OpenSSL closure, then rerun configure/build/CTest with matched protocol
compiler/header/runtime dependencies. Existing FUSE, disabled-test, coverage,
provisioning, acknowledged-write, recovery and disposable three-node gates stay
open. No service, mount, real data or consumer migration is included.

## OpenSSL contract checkpoint, November 6, 2026

Canonical development remains `02aaa408e77b98a252d26e33c5f2973f87b47c25`;
source parent is `fcb86c0cbc22e64c0e4e4c809b1a6bb05e3f0e4c` on the same
persistent task branch. `openssl-probe/` adds a reproducible C++20 dependency
contract with RAII contexts, an active SHA-256 known-answer check and a deliberate
wrong-digest control. Default static discovery fails because OpenSSL archives
are unavailable. No full-project or static runtime tests pass by implication.
See `openssl-validation.json` for exact results; shared-mode results are only a
probe diagnostic. Production sources and dependency requirements are unchanged.
Continue by qualifying static OpenSSL and the matched protocol closure, then
full build/CTest. All previously recorded FUSE/storage acceptance gaps remain.
