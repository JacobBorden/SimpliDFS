# SimpliDFS

SimpliDFS is a distributed file system project designed to manage files across multiple nodes. The system includes components for metadata management, file operations, message handling, and will soon include network communication between nodes for efficient file distribution and fault tolerance.

## Features

### 1. Metadata Management
- Manages file metadata using `MetadataManager`.
- Tracks file distribution and node associations.
- Thread-safe methods to add, retrieve, and remove metadata.

### 2. File System Operations
- Implements basic file operations: `createFile()`, `writeFile()`, and `readFile()`.
- Integrated with `MetadataManager` to keep metadata in sync with file operations.

### 3. Message Handling
- Uses a `Message` structure and `MessageType` enum to facilitate communication.
- `SerializeMessage()` and `DeserializeMessage()` are used to prepare messages for node communication.
- Metadata operations now notify nodes of updates.

## Current Progress
- The core functionality for metadata and file system operations has been implemented.
- Basic message handling is integrated for metadata updates.

## TODO List
- **Networking Integration**: Integrate [NetworkingLibrary](https://github.com/JacobBorden/NetworkingLibrary) to facilitate communication between nodes.
- **Node Management**: Implement node registration and heartbeat mechanisms.
- **Fault Tolerance**: Add replication of file chunks and handle node failures.
- **Metadata Persistence**: Develop a persistence layer for metadata.
- **Extended Operations**: Add support for file deletion and concurrency testing.
- **Testing and Documentation**: Expand unit tests and improve documentation.

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
   ./SimpliDFSTests
   ```

## Contributing
Feel free to contribute to the project by opening issues, submitting pull requests, or suggesting new features.

## License
This project is licensed under the MIT License.
