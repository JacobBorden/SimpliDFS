# Changelog

## [Unreleased]

### Added
- Unit tests for the core networking library functionalities (`Networking::Client`, `Networking::Server`).

### Changed
- Integrated an internal networking library (providing `Networking::Client` and `Networking::Server` classes) directly into the project source (`src/client.cpp`, `src/client.h`, `src/server.cpp`, `src/server.h`).
- Removed the old `networking_stubs.h` and updated `metaserver` and `node` components to use the new library.
- Replaced all stubbed network operations with calls to the integrated networking library.

### Fixed
- N/A

### Removed
- `src/networking_stubs.h` and its references.

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
