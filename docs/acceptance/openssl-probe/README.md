# OpenSSL dependency contract probe

This standalone C++20 probe links the same OpenSSL imported targets as
SimpliDFS_Utils. It owns digest and TLS contexts with RAII, checks the SHA-256
`abc` known answer with active Release checks, and exercises TLS context creation
and minimum-version configuration without opening a socket. It does not qualify
TLS handshakes, FIPS mode, key recovery or the full project's dependency closure.

From the task worktree:

```sh
cmake -S docs/acceptance/openssl-probe -B work/openssl-static -G Ninja
cmake --build work/openssl-static
ctest --test-dir work/openssl-static --output-on-failure
```

Static archives are required by default. If configure fails, stop: there is no
static runtime result. A successful future static build still requires inspection
of its link command and loaded libraries before claiming a fully static closure.
The archive suffix check does not qualify transitive dependencies or Windows
import libraries.

The shared diagnostic control uses a separate cache:

```sh
cmake -S docs/acceptance/openssl-probe -B work/openssl-shared -G Ninja \
  -DPROBE_STATIC_OPENSSL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build work/openssl-shared
ctest --test-dir work/openssl-shared --output-on-failure
work/openssl-shared/openssl-probe --wrong-digest
```

The last command must exit exactly 1; setup/API failures exit 3 or 4. This control
proves a wrong digest is rejected even with NDEBUG. Shared success is diagnostic
only and must never be recorded as static acceptance. The dated receipt is
`../openssl-validation.json`, including exact commands, toolchain, source hashes,
raw outcomes and loaded libraries.
