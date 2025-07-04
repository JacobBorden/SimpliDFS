## [0.11.48] - 2025-07-05
### Changed
- Restored contributor guidelines about testing in `AGENTS.md`.

## [0.11.49] - 2025-07-05
### Added
- Persistent state files for metaserver and nodes with configurable storage
  directory.

## [0.11.50] - 2025-07-05
### Fixed
- Node verifier thread now catches decryption errors when validating files,
  preventing crashes on startup.

## [0.11.47] - 2025-07-04
### Changed
- Release workflow injects `SIMPLIDFS_CLUSTER_KEY` into the metaserver service.

## [0.11.46] - 2025-07-04
### Changed
- Metaserver deployment passes `SIMPLIDFS_CLUSTER_KEY` to the container.

## [0.11.45] - 2025-07-03
### Changed
- FUSE stress tests moved to a separate GitHub Actions workflow.

## [0.11.44] - 2025-07-02
### Fixed
- Deployment workflow now stops any running container before starting the updated image.

## [0.11.43] - 2025-07-02
### Fixed
- Node now advertises its real IP address when registering with the MetadataManager.

## [0.11.42] - 2025-07-01
### Fixed
- Dockerfiles now keep the `-devel` suffix when downloading binaries so
  tagged development snapshots build correctly.

## [0.11.41] - 2025-06-30
### Fixed
- Release workflow now tags Docker images with the exact GitHub release
  version, so development snapshots use the `-devel` suffix.

## [0.11.40] - 2025-06-30
### Fixed
- Dockerfiles now pin the base image to `linux/amd64` even when built on
  arm64 hosts, preventing `exec format error` at runtime.

## [0.11.39] - 2025-06-30
### Changed
- Dockerfiles explicitly target the `linux/amd64` platform for x86-64 hardware.

## [0.11.38] - 2025-06-30
### Fixed
- Release workflow builds x86-64 Docker images to match the metaserver binary.

## [0.11.37] - 2025-06-29
### Changed
- Release workflow updates systemd unit with the deployed Docker tag.

## [0.11.33] - 2025-06-29
### Fixed
- CI and dependency scripts create a `libcares.a` symlink so static linking finds c‑ares.

## [0.11.34] - 2025-06-29
### Fixed
- Built-in libfuse step now produces static libraries so linking against `-lfuse3` succeeds.

## [0.11.35] - 2025-06-29
### Changed
- Logs are now written to `/var/logs/simplidfs/`. The metaserver logs to `metaserver.log` and nodes use `<NodeName>.log`.

## [0.11.36] - 2025-06-29
### Fixed
- Logger now outputs to both the console and log files so FUSE tests can parse redirected logs.

## [0.11.31] - 2025-06-29
### Fixed
- Added c-ares packages to CI workflows and dependency scripts so linking against `-lcares` succeeds.

## [0.11.32] - 2025-06-29
### Fixed
- Ensure runtime `libcares2` installs in CI workflows for successful linking.

## [0.11.30] - 2025-06-29
### Fixed
- Documented c-ares development requirement in README.

## [0.11.29] - 2025-06-28
### Fixed
- Release binaries now link OpenSSL statically for fully static builds.

## [0.11.28] - 2025-06-28
### Fixed
- Release binaries now link libzstd statically by honoring CMake options.

## [0.11.26] - 2025-06-28
### Fixed
- Release workflow now verifies binaries are statically linked to ensure they run without installed libraries.

## [0.11.27] - 2025-06-28
### Fixed
- Release binaries are now fully static by linking gRPC via pkg-config.

## [0.11.25] - 2025-06-28
### Fixed
- Release binaries are now statically linked and run without installed libraries.

## [0.11.23] - 2025-06-28
### Fixed
- Dependency scripts now install Protobuf 3.21.12 automatically if an older
  version is detected.

## [0.11.24] - 2025-06-28
### Fixed
- Dependency setup scripts now enforce Protobuf 3.21.12 even when a newer
  version is present.

## [0.11.22] - 2025-06-28
### Fixed
- Added protoc version check to `setup_dependencies_opensuse.sh` for consistency.

## [0.11.21] - 2025-06-28
### Fixed
- Documented required Protobuf version for SPIFFE and added version check in setup script.

## [0.11.18] - 2025-06-27
### Fixed
- Node now consistently connects to the provided MetadataManager address and
  port for heartbeats and verification instead of defaulting to 127.0.0.1.

## [0.11.17] - 2025-06-27
### Fixed
- Documented how to install the `simplidfs-metaserver.service` unit to avoid
  "Unit not found" errors during deployment.

## [0.11.16] - 2025-06-27
### Fixed
- Release workflow starts `simplidfs-metaserver.service` if it isn't running.

## [0.11.15] - 2025-06-27
### Fixed
- Release workflow explicitly restarts `simplidfs-metaserver.service`, avoiding unit lookup errors.

## [0.11.14] - 2025-06-27
### Fixed
- Release workflow restarts the `simplidfs-metaserver` service correctly.

## [0.11.12] - 2025-06-27
### Fixed
- Restart Metaserver now passes an access token for Artifact Registry login, preventing "Unauthenticated request" errors.

## [0.11.11] - 2025-06-27
### Fixed
- Metaserver restart step authenticates to Artifact Registry before pulling images.

## [0.11.8] - 2025-06-26
### Changed
- Release workflow deploys to GCP; `gcp_deploy.yml` removed.

## [0.11.7] - 2025-06-26
### Fixed
- GCP deploy workflow now runs on development tags using `workflow_run`.

## [0.11.6] - 2025-06-26
### Fixed
- Release workflow grants proper permissions to upload binaries.

## [0.11.5] - 2025-06-26
### Fixed
- Release workflow installs `libfuse3-dev` so release builds succeed.

## [0.11.4] - 2025-06-26
### Fixed
- Development tag workflow uses proper write permissions.

## [0.11.3] - 2025-06-26
### Fixed
- Dockerfiles strip the `-devel` suffix from the build argument so development
  tags resolve to existing release assets.

## [0.11.2] - 2025-06-26
### Changed
- Docker build workflow reads the repo version and pulls by digest.
- Release workflow now packages binaries as GitHub assets.

## [0.11.1] - 2025-06-25
### Added
- Deployment guide for Google Cloud with CI/CD workflow.

## [0.11.0] - 2025-06-25
### Changed
- Logger now formats messages via `json.hpp` instead of manual string escapes.

## [0.10.1] - 2025-06-25
### Fixed
- REST server correctly maps 401 responses, preventing test failures.
- S3 gateway closes client connections, resolving UploadAndDownload test
  timeouts.

## [0.10.0] - 2025-06-25
### Changed
- REST server and S3 gateway now use internal `http.hpp` utilities.
- JSON handling moved to lightweight `json.hpp`.

## [0.9.2] - 2025-06-25
### Added
- Dockerfiles for metaserver and node referencing the current release.
- `docker-compose.yml` for a devnet with monitoring stack.
- `make devnet` target to build images and start the stack.

## [0.9.1] - 2025-06-24
### Added
- Raft term and election metrics with Grafana panel.
- Prometheus alerts for election rate and replica lag.

## [0.9.0] - 2025-06-24
### Added
- JWT secret rotation script and daily cron schedule
- Token rotation tests for REST server

## [0.8.0] - 2025-06-24
### Added
- Automatic SVID retrieval from SPIRE when TLS is enabled.
- gRPC server now uses SPIRE-issued certificates by default.

## [0.7.1] - 2025-06-23
### Added
- rebootstrap_raft.sh script resets Raft state and restarts nodes.

## [0.7.0] - 2025-06-23
### Added
- Raft snapshot transfer API and log compaction support.


## [0.6.2] - 2025-06-22
### Changed
- SBOM generation workflow steps now run only on pushes to the `main` branch.

## [0.6.1] - 2025-06-22
### Added
- CI now publishes CycloneDX and SPDX SBOMs signed with cosign.

## [0.6.0] - 2025-06-21
### Added
- MerkleTree now stores directory nodes and can produce membership proofs.
- `simplidfs verify <cid>` command verifies chunks using stored proofs.

## [0.5.1] - 2025-06-21
### Added
- Raft now commits Merkle root hashes for every metadata update and exposes
  commit metrics via the Prometheus endpoint.

## [0.5.0] - 2025-06-20
### Changed
- Default encryption uses XChaCha20-Poly1305 with AES-GCM fallback.

## [0.4.0] - 2025-06-20
### Added
- TPM attestation on node join using PCR 7 quotes.
- Edge node role generates quotes and passes them to the metaserver.

## [0.3.64] - 2025-06-20
### Added
- seekpWithRetry helper improves FUSE concurrency tests.
### Changed
- Random write test uses seekpWithRetry and new unit tests verify it.

## [0.3.63] - 2025-06-18
### Added
- CI pipeline now executes FUSE stress tests on every run.
### Changed
- Stress tests cover overlapping writes and fsync batching behavior.

## [0.3.61] - 2025-06-17
### Added
- Extent-level locking and append batching in FUSE adapter to prevent race corruption.

#Changelog
## [0.3.60] - 2025-06-16
### Changed
- Data pipeline now compresses before encryption then hashes, reducing chunk store size.

## [0.3.59] - 2025-06-16
### Added
- Unit test for writing and reading files directly through a Node without FUSE.
## [0.3.58] - 2025-06-16
### Added
- Basic FUSE mount, write/read and append tests for easier debugging.
## [0.3.57] - 2025-06-15
### Added
- Single-thread variants of `FuseRandomWriteTest` and `FuseAppendTest` to aid debugging of concurrency issues.

## [0.3.56] - 2025-06-15
### Fixed
- `create` now assigns a unique file handle, matching the `open` behavior. This
  prevents handle collisions when new files are created through FUSE.

## [0.3.55] - 2025-06-15
### Fixed
- FUSE adapter assigns unique file handles during `open`, preventing handle
  collisions that broke concurrent append and random write tests.
## [0.3.54] - 2025-06-13
### Fixed
- Retried file openings now clear fail states to succeed when the file appears,
  preventing false failures in `FuseRandomWriteTest` and `FuseAppendTest`.
## [0.3.53] - 2025-06-13
### Fixed
- Increased wait time for node registrations in the FUSE concurrency wrapper
  to prevent sporadic failures in `FuseRandomWriteTest` and `FuseAppendTest`.
## [0.3.52] - 2025-06-11
### Fixed
- FuseRandomWriteTest now retries opening the result file before verification,
  avoiding sporadic failures when the filesystem needs extra time.
- Wrapper script cleans up storage nodes only if they are running, preventing
  "Aborted" process messages.
## [0.3.51] - 2025-06-11
### Fixed
- Storage nodes now apply writes at explicit offsets, enabling random write and
  append operations in the FUSE adapter.
## [0.3.50] - 2025-06-11
### Fixed
- Wrapper verifies storage nodes register with the metaserver before starting
  the FUSE adapter, preventing random write and append tests from failing when
  the metaserver is unavailable.
## [0.3.49] - 2025-06-11
### Fixed
- Fuse concurrency wrapper now prefixes test executables with `./` so CTest
  can locate them in the build directory.

## [0.3.48] - 2025-06-11
### Added
- Split FuseConcurrencyTest into separate RandomWrite and Append tests for easier debugging.


## [0.3.47] - 2025-06-10
### Fixed
- FuseConcurrencyTest reads mount point from `SIMPLIDFS_CONCURRENCY_MOUNT`
  environment variable to match wrapper script.

## [0.3.46] - 2025-06-10
### Fixed
- Create per-run temporary directories for FuseConcurrencyTest logs and mount
  point to avoid permission issues from stale files.

## [0.3.45] - 2025-06-10
### Fixed
- Remove stale metadata before FuseConcurrencyTest to avoid partial replication.

## [0.3.44] - 2025-06-10
### Fixed
- Ensure protobuf sources are generated before compilation using a new `generate_protos` CMake target.
- Document removing stale build artifacts prior to configuring.

## [0.3.43] - 2025-06-10
### Fixed
- Remove stale generated protobuf files and update include paths to use
  build-time generated sources.

## [0.3.42] - 2025-06-10
### Fixed
- Regenerate gRPC sources during the build to match installed Protocol Buffers.

## [0.3.41] - 2025-06-10

### Added
- Added unit tests for MetricsRegistry and MerkleTree utilities to improve
  coverage across the project.

## [0.3.40] - 2025-06-10

### Fixed
- Removed duplicate main declaration in `fuse_concurrency_tests.cpp` to restore test compilation.

## [0.3.39] - 2025-06-09

### Added
- Implemented `truncate` and `fallocate` operations in the FUSE adapter.
- Introduced `TruncateFile` message and metadata support.
- Added unit test verifying truncate updates file size.

## [0.3.38] - 2025-06-09

### Fixed
- Always call `posix_fallocate` after `ftruncate` and fall back to manual seek/write if needed. This guarantees correct preallocation on FUSE.

## [0.3.37] - 2025-06-09

### Fixed
- Stored file paths alongside open handles so `simpli_write` can recover the
  path when FUSE provides an empty string. This resolves preallocation failures
  in `FuseConcurrencyTest`.

## [0.3.36] - 2025-06-09

### Fixed
- Added additional fallback logic to `preallocateFile` for filesystems lacking
  `posix_fallocate` support, resolving test setup failures on FUSE.

## [0.3.35] - 2025-06-08

### Fixed
- Improved FuseConcurrencyTest verification to address header, line count, and hash mismatches.

## [0.3.34] - 2025-06-08

### Added
- Test for `compute_sha256` helper function.

### Changed
- FuseConcurrencyTest now only fails if the final SHA-256 hash differs from the expected contents.

## [0.3.33] - 2025-06-08

### Changed
- FuseConcurrencyTest now verifies success by comparing SHA-256 hashes of the expected and actual test file.

## [0.3.32] - 2025-06-08

### Fixed
- CMake uses system-installed gRPC to avoid network downloads that caused
  configuration stalls. FuseConcurrencyTest now builds and runs reliably.

## [0.3.31] - 2025-06-08

### Fixed
- FuseConcurrencyTest now skips if the FUSE mount operation fails due to missing permissions.

## [0.3.30] - 2025-06-08

### Fixed
- Added portable fallback logic to `preallocateFile` so FUSE filesystems
  without full `ftruncate` support can still run `FuseConcurrencyTest`.

## [0.3.29] - 2025-06-08

### Fixed
- Reverted mount detection change in the FUSE test wrapper and restored the
  original sleep-based check.

## [0.3.28] - 2025-06-08

### Changed
- Updated `writeFileData` logging to reflect active writes to nodes.
- Documented `readFileData` and `writeFileData` with Doxygen comments.

## [0.3.27] - 2025-06-08

### Fixed
- Prevented double disconnection of clients in `HandleClientConnection` by
  checking connection status before calling `DisconnectClient`.
- Added `Server::isClientConnected` helper and associated unit test.

## [0.3.26] - 2025-06-08

### Changed
- Preallocated file in `FuseConcurrencyTest` to avoid failures when seeking
  beyond EOF.
- Added unit test for `preallocateFile` helper.

## [0.3.25] - 2025-06-08

### Fixed
- Adjusted `NodeHealthTracker` to use millisecond thresholds so tests compile
  correctly.

## [0.3.24] - 2025-06-07

### Fixed
- Resolved compilation error in `FuseConcurrencyTest` caused by an unused
  variable in `getFuseTestTimestamp`.

## [0.3.23] - 2025-06-07

### Added
- Unit tests for `NodeHealthTracker`.

### Changed
- Expanded comments in `node_health_tracker.cpp` for clarity.

## [0.3.22] - 2025-06-07

### Changed
- Enforced test dependency so `FuseConcurrencyTest` runs after `FuseTestEnvSetup`.

## [0.3.21] - 2025-06-07

### Added
- Counter `simplidfs_replication_failures` for tracking failed replica checks.
- Grafana panel for replication failures.

### Changed
- Updated README and progress documentation to reflect working replication and re-replication.

## [0.3.20] - 2025-06-07

### Added
- Project overview and documentation index in `docs/overview.md` and `docs/README.md`.
- `Doxyfile` for generating API reference documentation.
- Documentation section in `README.MD` pointing to design docs.

## [0.3.19] - 2025-06-07

### Fixed
- Avoid duplicate protobuf target errors by relying on gRPC's bundled protobuf.

## [0.3.18] - 2025-06-07

### Changed
- Updated CMake policies to silence warnings on newer CMake versions.
- Added explicit gRPC package lookup to ensure `gRPC::grpc++` target is available.

## [0.3.17] - 2025-06-07

### Added
- Prometheus gauges `simplidfs_tier_bytes` and `simplidfs_replication_pending` for monitoring tier usage and replication backlog.
- Dashboard panels for these metrics.

## [0.3.16] - 2025-06-07

### Added
- Configurable hashing/compression/encryption pipeline in `BlockIO`.
- Runtime configuration via `simplidfs_config.yaml` for compression level and cipher algorithm.

## [0.3.15] - 2025-06-07

### Added
- Concurrent metadata test covering file create/delete, node registration,
  heartbeats and dead-node checks.

## [0.3.14] - 2025-06-07

### Added
- KeyManager initialization during node and metaserver startup.
- FileSystem encryption now uses the cluster key from KeyManager.
- Documentation on setting `SIMPLIDFS_CLUSTER_KEY` or allowing auto generation.

### Changed
- Unit tests initialize KeyManager and verify encryption with the cluster key.

## [0.3.13] - 2025-06-07

### Added
- setup_dependencies_opensuse.sh for installing dependencies on openSUSE via zypper.

## [0.3.12] - 2025-06-07

### Fixed
- GitHub workflows now install `libyaml-cpp-dev` so CMake can find yaml-cpp.

## [0.3.11] - 2025-06-07

### Fixed
- CI workflow now installs Boost to avoid missing dependency errors.

## [0.3.10] - 2025-06-07

### Added
- gRPC and Protobuf development packages in GitHub workflow and
  `setup_dependencies.sh` to ensure builds succeed.

## [0.3.9] - 2025-06-07

### Added
- IPFS gateway with JWT-protected block retrieval.

## [0.3.8] - 2025-06-07

### Added
- Minimal S3 gateway translating PUT and GET requests to SimpliDFS file operations.
- Integration test for uploads and downloads via the gateway.

## [0.3.7] - 2025-06-07
### Added
- Re-replication now performs actual data transfer when nodes fail.
- RepairWorker triggers replication callbacks for partial files.

## [0.3.6] - 2025-06-07

### Added
- Configurable compression level and cipher algorithm loaded at runtime.

## [0.3.5] - 2025-06-07

### Added
- POSIX compliance tests runnable via `-DBUILD_POSIX_TEST_SUITE=ON`.

## [0.3.4] - 2025-06-07

### Added
- Metaserver and node load TLS certificates via command-line flags and enable TLS.
- README examples show TLS-enabled startup commands.

## [0.3.3] - 2025-06-07

### Added
- Lightweight REST server with JWT authentication in `src/rest_server.cpp`.
- Documentation for the HTTP API in `docs/rest_api.md`.

## [0.3.2] - 2025-06-07

### Added
- Chaos test now provisions an edge node with Ansible, repeatedly kills it,
  runs latency benchmarks and prints availability metrics.

## [0.3.1] - 2025-06-07

### Added
- FIPS self test at startup.

## [0.3.0] - 2025-06-07

### Added
- Cluster key rotation APIs with configurable window.
- `scripts/rotate_key.sh` helper for rotating keys.
- Documentation in `docs/key_rotation.md`.

## [0.2.10] - 2025-06-07

### Added
- Tiered storage deployment playbook with monitoring steps.

## [0.2.9] - 2025-06-07

### Added
- Dependency installation script now installs `libyaml-cpp-dev`.

## [0.2.8] - 2025-06-07

### Added
- Initial FedRAMP RMF control mapping in `docs/fedramp_mapping.md`.

## [0.2.7] - 2025-06-07

### Added
- YAML-defined RBAC policy with middleware checks for node operations.

## [0.2.6] - 2025-06-07

### Added
- SHA-linked audit log for create, write and delete operations.
- AuditVerifier background job to validate the log chain daily.

## [0.2.5] - 2025-06-07

### Added
- Prometheus metrics for cluster state, replica health and FUSE latency.
- Default Grafana dashboard in `monitoring/grafana`.

## [0.2.4] - 2025-06-07

### Added
- TLS support for `Networking::Client` and `Networking::Server`.
- Script `scripts/generate_node_cert.sh` for generating test certificates.

### Changed
- CMake build links against OpenSSL.

### Fixed
- N/A

## [0.2.3] - 2025-06-07

### Added
- Delta-only synchronization for nodes transitioning from offline to online.

### Changed
- N/A

### Fixed
- N/A

## [0.2.2] - 2025-06-07

### Added
- RepairWorker now enforces a replication factor of three and heals missing replicas.
- ReplicaVerifier checks hashes across nodes to detect inconsistent replicas.

### Changed
- N/A

### Fixed
- N/A

## [0.2.1] - 2025-06-07

### Added
- Metadata operations now append commands to the Raft log for replication.

### Changed
- The metaserver injects its RaftNode instance into `MetadataManager` at startup.

### Fixed
- N/A

## [0.2.0] - 2025-06-06

### Added
- Unit tests for the core networking library functionalities (`Networking::Client`, `Networking::Server`).
- Fuzz testing suite integrated with libFuzzer (build with `-DBUILD_FUZZING=ON`).
- Chaos test validating leader death recovery in the Raft implementation.

### Changed
- Integrated an internal networking library (providing `Networking::Client` and `Networking::Server` classes) directly into the project source (`src/client.cpp`, `src/client.h`, `src/server.cpp`, `src/server.h`).
- Removed the old `networking_stubs.h` and updated `metaserver` and `node` components to use the new library.
- Replaced all stubbed network operations with calls to the integrated networking library.

### Fixed
- N/A

### Removed
- `src/networking_stubs.h` and its references.
- Deprecated `FuseConcurrencyTest` in favour of fuzz testing.

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2024-08-04

### Added
- Node registration with the MetadataManager.
- Heartbeat mechanism for nodes to report liveness.
- Metadata persistence to disk for `fileMetadata` and `registeredNodes`.
- File deletion support in `FileSystem`, `MetadataManager`, and propagated to nodes (stubbed network call).
- Replication strategy during file creation in `MetadataManager` to use a default replication factor.
- Logic in `MetadataManager` to detect node failures and identify files needing re-replication.
- Doxygen-style documentation for core components (`MetadataManager`, `Node`, `FileSystem`, `Message`).
- Conceptual concurrency test plan (`tests/CONCURRENCY_TEST_IDEAS.md`).
- `ReplicateFileCommand` and `ReceiveFileCommand` message types for future use in data transfer.
- `RegisterNode`, `Heartbeat`, `DeleteFile` message types.

### Changed
- Refactored message serialization/deserialization functions into static methods of the `Message` class.
- Updated `README.MD` and `PROGRESS.MD` to reflect current project status.

### Fixed
- N/A (Initial feature release for these components)

### Removed
- N/A
