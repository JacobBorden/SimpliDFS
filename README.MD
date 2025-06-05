#SimpliDFS

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
- Note: Actual network communication for data transfer (including initial replication and re-replication) now utilizes the integrated networking library.
- Default replication factor is **3** and block writes are replicated to all replicas.
- Each storage node runs a background verifier thread that checks file hashes and heals corrupted replicas by fetching a clean copy from peers.

## Current Progress
Significant progress has been made on core distributed file system logic:
- Robust metadata management with persistence.
- Node registration and liveness tracking (heartbeats).
- Fault tolerance framework including file replication strategy and detection of node failures for future re-replication.
- File deletion support.
- Initial code refactoring (messaging system) and comprehensive Doxygen documentation for key components.
- Network communication between nodes and the metaserver is handled by the integrated networking library.

## TODO List
- **Networking Integration**: Completed. The networking library is now integrated directly into the project source (`src/client.h`, `src/server.h`) and is used by metaserver and node components.
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
2. Run the setup script to install dependencies:
   ```sh
   ./setup_dependencies.sh
   ```
3. Create a build directory and run CMake:
   ```sh
   mkdir build && cd build
   cmake ..
   make
   ```
4. Run the tests:
   ```sh
   ./test/SimpliDFSTests
   ```

### Optional: Fuzz Testing
SimpliDFS includes fuzzers built with libFuzzer to stress test core components.
To build them, configure the project with the `BUILD_FUZZING` option enabled and
use Clang:

```sh
cmake -B build -S . -DBUILD_FUZZING=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

The resulting fuzzer executables are located in `build/tests/`. Refer to
`TESTING_GUIDE.md` for usage details.

### Running a Node
After building, you can start a storage node with:

```sh
./node <NodeName> <Port> <MetaserverAddress> <MetaserverPort>
```

For example, to run a node named `NodeA` on port `60010` that connects to a
metaserver running on `127.0.0.1` port `50505`:

```sh
./node NodeA 60010 127.0.0.1 50505
```

### Snapshot CLI Example
The `snapshot` command lets you capture the current state of the in-memory file
system and restore it later.  Rolling back a corrupted file takes only a few
steps:

```sh
# 1. Capture a snapshot of the healthy state
./simpli_fuse_adapter snapshot create clean

# 2. Restore after corruption
./simpli_fuse_adapter snapshot checkout clean

# 3. Inspect changes relative to the snapshot
./simpli_fuse_adapter snapshot diff clean
```

## Contributing
Feel free to contribute to the project by opening issues, submitting pull requests, or suggesting new features.

## License
This project is licensed under the MIT License.

## Chaos Testing
The `tests/chaos_kill_node.sh` script simulates node failures by randomly killing
one storage node every five minutes for two hours. Use this after launching the
metaserver and nodes to evaluate replication healing.

## Security
SimpliDFS relies on a cluster-wide encryption key. Set the
`SIMPLIDFS_CLUSTER_KEY` environment variable on every node before starting the
metaserver or storage nodes to ensure they share the same key. The value must be
a 64-character hex string (32 bytes). Example:

```sh
export SIMPLIDFS_CLUSTER_KEY=$(openssl rand -hex 32)
```

If this variable is missing or malformed, each process generates a new random
key, preventing other nodes from decrypting its data. See
[SECURITY.md](SECURITY.md) for details.
