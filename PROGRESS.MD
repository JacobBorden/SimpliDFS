# SimpliDFS Project - Current Progress and TODO List

## Current Progress

### 1. Metadata Management System
- Implemented `MetadataManager` class to manage metadata about files stored across nodes.
- Provides thread-safe methods for adding, retrieving, and removing file metadata.
- Messages are serialized and used to notify nodes when files are added or removed.

### 2. File System Operations
- Basic file operations implemented in `filesystem.h` and `filesystem.cpp`:
  - `createFile()`, `writeFile()`, and `readFile()` to manage file data.
- Integrated with `MetadataManager` to track which nodes hold file data.

### 3. Message Handling System
- Defined `Message` structure and `MessageType` enumeration in `message.h`.
- Implemented `SerializeMessage()` and `DeserializeMessage()` to convert `Message` objects to/from strings for communication between nodes.
- Metadata operations now send appropriate messages to nodes for file creation, writing, and deletion.

## TODO List

### 1. Networking Integration
- **Integrate Networking Library**: Use [NetworkingLibrary](https://github.com/JacobBorden/NetworkingLibrary) to enable actual communication between nodes.
  - Implement message sending and receiving capabilities for distributed coordination.
  - Replace placeholders for message sending in `MetadataManager` with real network operations.

### 2. Node Management
- **Node Registration**: Implement a system to register nodes with the master (metadata manager).
- **Heartbeat Mechanism**: Add a mechanism to ensure nodes are active by implementing periodic heartbeat messages.

### 3. Fault Tolerance
- **Replication**: Implement replication of file chunks across multiple nodes for fault tolerance.
- **Failure Handling**: Define behavior when a node goes offline, including redistribution of its file chunks.

### 4. Metadata Persistence
- Implement a persistence layer for metadata to ensure state is retained across restarts (e.g., using a simple file-based storage or a small database like SQLite).

### 5. Extended File System Operations
- **Delete File**: Expand `FileSystem` and `MetadataManager` to support file deletion across nodes.
- **Concurrency Testing**: Test thread safety and concurrency issues for both the file system and metadata management, especially under high load.

### 6. Code Refactoring and Documentation
- **Refactor Messaging**: Improve the messaging system to make it more flexible for different types of operations.
- **Documentation**: Add comprehensive documentation for each module, including how each piece fits together.
- **Unit Testing**: Write unit tests for `MetadataManager`, `FileSystem`, and messaging functionality to ensure reliability.

## Next Steps
- Begin by integrating the networking layer for real-time communication between nodes.
- Work on implementing a node registration and heartbeat system to maintain awareness of the cluster state.
- Address metadata persistence for ensuring data integrity across restarts.

