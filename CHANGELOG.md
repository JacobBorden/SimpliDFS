# Changelog
## [0.3.0] - 2025-06-11

### Added
- Cluster key rotation APIs with configurable window.
- `scripts/rotate_key.sh` helper for rotating keys.
- Documentation in `docs/key_rotation.md`.

## [0.2.9] - 2025-06-10

### Added
- Dependency installation script now installs `libyaml-cpp-dev`.

## [0.2.8] - 2025-06-09

### Added
- Initial FedRAMP RMF control mapping in `docs/fedramp_mapping.md`.

## [0.2.7] - 2025-06-08

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
