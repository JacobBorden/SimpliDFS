# Changelog

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

## [0.3.22] - 2025-06-07

### Changed
- Enforced test dependency so `FuseConcurrencyTest` runs after `FuseTestEnvSetup`.

## [0.3.23] - 2025-06-07

### Added
- Unit tests for `NodeHealthTracker`.

### Changed
- Expanded comments in `node_health_tracker.cpp` for clarity.

## [0.3.24] - 2025-06-07

### Fixed
- Resolved compilation error in `FuseConcurrencyTest` caused by an unused
  variable in `getFuseTestTimestamp`.

## [0.3.25] - 2025-06-08

### Fixed
- Adjusted `NodeHealthTracker` to use millisecond thresholds so tests compile
  correctly.

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
