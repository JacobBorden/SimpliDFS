# SimpliDFS

SimpliDFS is a distributed file system project designed to manage files across multiple nodes. The system includes components for metadata management, file operations, message handling, and will soon include network communication between nodes for efficient file distribution and fault tolerance.

## Features

### 1. Metadata Management
- Manages file metadata using `MetadataManager`.
- Tracks file distribution and node associations.
- Thread-safe methods to add, retrieve, and remove metadata.
- Manages a registry of active storage nodes.
- Persists metadata (file locations, node registry) to disk to ensure durability across metaserver restarts.

### 2. File System Operations
- Implements basic file operations: `createFile()`, `writeFile()`, and `readFile()`.
- Supports file deletion, propagating changes to relevant nodes.
- Integrated with `MetadataManager` to keep metadata in sync with file operations.

### 3. Message Handling
- Uses a `Message` structure and `MessageType` enum to facilitate communication.
- `SerializeMessage()` and `DeserializeMessage()` are now static methods of the `Message` class.
- Metadata operations now notify nodes of updates.

### 4. Node Management
- Nodes can register with the metaserver.
- A heartbeat mechanism allows nodes to report their status and helps the metaserver detect failures.

### 5. Fault Tolerance (Conceptual)
- Implements a replication strategy during file creation to distribute multiple copies of files across available live nodes.
- Includes logic to detect node failures (via heartbeats) and identify files needing re-replication.
- Note: Actual network communication for data transfer (including initial replication and re-replication) is currently stubbed due to an inaccessible networking library.

## Current Progress
Significant progress has been made on core distributed file system logic:
- Robust metadata management with persistence.
- Node registration and liveness tracking (heartbeats).
- Fault tolerance framework including file replication strategy and detection of node failures for future re-replication.
- File deletion support.
- Initial code refactoring (messaging system) and comprehensive Doxygen documentation for key components.
- Network communication between nodes and the metaserver is currently stubbed.

## TODO List
- **Networking Integration**: Integrate a networking library to enable communication between nodes (currently stubbed; original [NetworkingLibrary](https://github.com/JacobBorden/NetworkingLibrary) was inaccessible).
- **Node Management**: Core logic implemented (registration/heartbeat complete). Further enhancements for dynamic node property updates could be considered.
- **Fault Tolerance**: Core logic implemented (replication strategy, failure detection complete; re-replication data transfer stubbed). Full end-to-end testing with a live networking layer is pending.
- **Metadata Persistence**: Complete.
- **Extended Operations**:
    - File deletion: Complete.
    - Concurrency testing: Conduct rigorous concurrency testing (initial review and test ideas documented).
- **Testing and Documentation**:
    - Expand unit tests for all major components.
    - Doxygen documentation: Initial comprehensive documentation for core modules complete.
- **File Encryption**: Implement file encryption for data at rest.

## Getting Started
### Prerequisites
- CMake 3.10 or later.
- A C++ compiler (e.g., GCC or Clang).
- Google Test for unit testing.

### Build Instructions
1. Clone the repository:
   ```sh
   git clone https://github.com/JacobBorden/SimpliDFS.git
   cd SimpliDFS
   ```
2. Create a build directory and run CMake:
   ```sh
   mkdir build && cd build
   cmake ..
   make
   ```
3. Run the tests:
   ```sh
   ./test/SimpliDFSTests
   ```

## Contributing
Feel free to contribute to the project by opening issues, submitting pull requests, or suggesting new features.

## License
This project is licensed under the MIT License.
