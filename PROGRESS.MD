#SimpliDFS Project - Current Progress and TODO List

## Current Progress

### 1. Metadata Management System
- Implemented `MetadataManager` class to manage metadata about files stored across nodes.
- Provides thread-safe methods for adding, retrieving, and removing file metadata.
- Includes management of a node registry with liveness tracking.
- Metadata (file locations, node registry) is persisted to disk and reloaded on startup.
- Messages are serialized and used to notify nodes when files are added or removed.

### 2. File System Operations
- Basic file operations implemented in `filesystem.h` and `filesystem.cpp`:
  - `createFile()`, `writeFile()`, and `readFile()` to manage file data.
  - `deleteFile()` operation implemented for removing files.
- Integrated with `MetadataManager` to track which nodes hold file data and propagate delete operations.

### 3. Message Handling System
- Defined `Message` structure and `MessageType` enumeration in `message.h`.
- `SerializeMessage()` and `DeserializeMessage()` are now static methods of the `Message` class.
- Metadata operations now send appropriate messages to nodes for file creation, writing, and deletion.

### 4. Node Management
- Implemented a system for nodes to register with the `MetadataManager`.
- Added a heartbeat mechanism for nodes to report their active status, allowing the `MetadataManager` to detect unresponsive nodes.

### 5. Fault Tolerance (Core Logic)
- Implemented a file replication strategy within `MetadataManager::addFile` to ensure files are replicated across a configurable number of live nodes.
- Developed logic in `MetadataManager::checkForDeadNodes` to identify files affected by node failures and re-replicate them using the networking library.

### 6. Documentation
- Added comprehensive Doxygen-style documentation to core classes and methods in `metaserver.h`, `node.h`, `filesystem.h`, and `message.h`.

## TODO List

### 1. Networking Integration
- **Networking Integration**: Completed.
  - The internal networking library (`src/client.h`, `src/server.h`) has been integrated.
  - Message sending and receiving capabilities are now implemented using this library for distributed coordination.
  - Placeholders for message sending in `MetadataManager` and `Node` have been replaced with real network operations.

### 2. Node Management
- **Node Registration**: Complete.
- **Heartbeat Mechanism**: Complete.

### 3. Fault Tolerance
 - **Replication**: Implement replication of file chunks and full re-replication using the networking library.
 - **Failure Handling**: Node failure detection, selection of new replica targets, and data transfer are fully implemented.
- **Background Verification**: Nodes now periodically verify local file hashes and automatically heal corrupted copies by fetching clean data from peers.
- **Replication Factor**: System defaults to a replication factor of 3 for all blocks.

### 4. Metadata Persistence
- **Metadata Persistence**: Complete.

### 5. Extended File System Operations
- **Delete File**: Complete.
- **Concurrency Testing**: Conduct rigorous concurrency testing (Initial review and test ideas documented in `tests/CONCURRENCY_TEST_IDEAS.MD`).

### 6. Code Refactoring and Documentation
- **Refactor Messaging**: Complete (Serialization/deserialization moved to `Message` class static methods).
- **Documentation**: Initial comprehensive Doxygen documentation for core modules is complete.
- **Unit Testing**: Write unit tests for `MetadataManager`, `FileSystem`, and messaging functionality to ensure reliability.
- **Fuzz Testing**: Integrated libFuzzer-based fuzzers with optional CMake build
  flag (`BUILD_FUZZING`) and a GitHub Actions workflow.

### 7. Security
- **File Encryption**: Implement file encryption for data at rest.

## Next Steps
  - Complete end-to-end testing of replication and re-replication mechanisms.
  - Develop comprehensive unit tests for all implemented features.
  - Begin work on file encryption.
