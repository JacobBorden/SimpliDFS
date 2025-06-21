/**
 * @file metaserver.h
 * @brief Defines the MetadataManager class and NodeInfo struct for the
 * SimpliDFS system.
 *
 * The MetadataManager is the central authority for tracking file metadata,
 * node registration, node liveness (via heartbeats), and orchestrating file
 * replication and data persistence.
 */
#include "cluster/NodeHealthCache.h"
#include "metaserver/node_health_tracker.h"
#include "utilities/chunk_store.hpp"
#include "utilities/filesystem.h" // Included for context, though not directly used in this header
#include "utilities/merkle_tree.hpp"
#include "utilities/message.h" // For Message struct and MessageType enum
#include "utilities/raft.h"
#include <algorithm> // Required for std::find
#include <atomic>    // For std::atomic<bool>
#include <ctime>     // Required for time(nullptr)
#include <fstream>   // For std::ofstream, std::ifstream
#include <iostream>
#include <memory>
#include <sstream>   // For std::stringstream
#include <stdexcept> // For std::invalid_argument in std::stol
#include <string>
#include <thread>
#include <unordered_map> // Required for std::unordered_map
#include <unordered_set>
#include <vector>

// Persistence constants
/** @brief Separator character used in metadata persistence files. */
const char METADATA_SEPARATOR = '|';
/** @brief Separator character for lists of nodes in metadata persistence files.
 */
const char NODE_LIST_SEPARATOR = ',';

/// Error codes specific to MetadataManager operations
const int ERR_NO_REPLICA = 2001;
const int ERR_INSUFFICIENT_REPLICA = 2002;

/**
 * @brief Holds information about a registered storage node.
 */
struct NodeInfo {
  std::string nodeAddress; ///< Network address of the node (e.g., "ip:port").
  time_t registrationTime; ///< Timestamp of when the node first registered.
  time_t lastHeartbeat; ///< Timestamp of the last heartbeat received from this
                        ///< node.
  bool isAlive; ///< Current liveness status of the node (true if responsive,
                ///< false if timed out).
  // Potentially other info: capacity, load, etc. could be added here.
};

/** @brief Timeout in seconds. If a node doesn't send a heartbeat within this
 * period, it's marked as not alive. */
const int NODE_TIMEOUT_SECONDS = 30;

/**
 * @brief Manages all metadata for the SimpliDFS system.
 *
 * This class is responsible for:
 * - Tracking registered storage nodes and their liveness via heartbeats.
 * - Managing file metadata, including which nodes store replicas of each file.
 * - Implementing a replication strategy for file creation and handling node
 * failures.
 * - Persisting its state (file metadata and node registry) to disk and loading
 * it on startup. All public methods are thread-safe.
 */
class MetadataManager {
private:
  // Mutex to protect all shared metadata (fileMetadata, registeredNodes).
  // Critical for all operations.
  std::mutex metadataMutex; ///< Mutex ensuring thread-safe access to internal
                            ///< data structures.

  /** @brief Maps filenames to a list of node identifiers that store replicas of
   * the file. */
  std::unordered_map<std::string, std::vector<std::string>> fileMetadata;

  /** @brief Maps node identifiers to NodeInfo structs containing details about
   * each registered node. */
  std::unordered_map<std::string, NodeInfo> registeredNodes;

  // New private members for FUSE attributes
  std::unordered_map<std::string, uint32_t> fileModes;
  std::unordered_map<std::string, uint64_t> fileSizes;
  std::unordered_map<std::string, std::string> fileHashes;
  std::atomic<bool> metadata_is_dirty_{false};

  /** @brief Mutex protecting per-file lock map. */
  mutable std::mutex fileLockMapMutex;
  /** @brief Map of file names to write mutexes used to serialize concurrent
   * writes. */
  std::unordered_map<std::string, std::shared_ptr<std::mutex>> fileWriteLocks;

  /**
   * @brief Retrieve (or create) the mutex protecting a file's metadata.
   * @param filename File name whose mutex is requested.
   * @return Shared pointer to a mutex guarding that file.
   */
  std::shared_ptr<std::mutex> getFileWriteLock(const std::string &filename) {
    std::lock_guard<std::mutex> guard(fileLockMapMutex);
    auto it = fileWriteLocks.find(filename);
    if (it == fileWriteLocks.end()) {
      it = fileWriteLocks.emplace(filename, std::make_shared<std::mutex>())
               .first;
    }
    return it->second;
  }

  /** @brief Mutex protecting the active client set. */
  mutable std::mutex clientMutex;
  /** @brief Set of active client thread ids for debugging. */
  std::unordered_set<std::thread::id> activeClients;

  /**
   * @brief Waits for metadata about a file to appear.
   *
   * Helper used by read/write operations to reduce races when
   * metadata updates are slightly delayed.
   * @param filename File to check.
   * @param retries  Number of retries.
   * @param delay_ms Delay between retries in milliseconds.
   * @return True if metadata became available.
   */
  bool waitForFileMetadata(const std::string &filename, int retries = 2,
                           int delay_ms = 10);

  NodeHealthTracker healthTracker_;
  NodeHealthCache healthCache_;
  RaftNode *raftNode_{nullptr};
  ChunkStore namespaceStore_;
  std::string currentRootCid_;
  std::atomic<size_t> commitIndex_{0};

  /** @brief Default number of replicas to create for each file. */
  static const int DEFAULT_REPLICATION_FACTOR = 3;

public:
  /**
   * @brief Constructs a MetadataManager object.
   * @note Metadata loading from persistence files is typically handled
   * separately after construction (e.g., in main).
   */
  MetadataManager() {
    // loadMetadata is called from metaserver.cpp after instantiation
  }

  /**
   * @brief Inject a RaftNode for log replication.
   * @param node Pointer to an existing RaftNode instance.
   */
  void setRaftNode(RaftNode *node) { raftNode_ = node; }

  void applyRaftLog(const std::vector<RaftLogEntry> &log);
  void updateMerkleRoot();
  size_t getCommitIndex() const { return commitIndex_.load(); }
  std::string getMerkleRoot() {
    std::lock_guard<std::mutex> lock(metadataMutex);
    return currentRootCid_;
  }

  /**
   * @brief Registers a new storage node or updates information for an existing
   * one. Initializes the node's registration time and last heartbeat time.
   * Marks the node as alive.
   * @param nodeIdentifier A unique string identifying the node.
   * @param nodeAddr The network address (IP or hostname) of the node.
   * @param nodePrt The port number the node is listening on.
   */
  void registerNode(const std::string &nodeIdentifier,
                    const std::string &nodeAddr, int nodePrt) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    NodeInfo newNodeInfo;
    newNodeInfo.nodeAddress = nodeAddr + ":" + std::to_string(nodePrt);
    newNodeInfo.registrationTime = time(nullptr);
    newNodeInfo.lastHeartbeat = time(nullptr); // Initialize lastHeartbeat
    newNodeInfo.isAlive = true;

    registeredNodes[nodeIdentifier] = newNodeInfo;
    healthCache_.recordSuccess(nodeIdentifier);
    std::cout << "Node " << nodeIdentifier << " registered from " << nodeAddr
              << ":" << nodePrt << std::endl;
    if (raftNode_)
      raftNode_->appendCommand("REG|" + nodeIdentifier);
  }

  // Process a heartbeat message from a node
  void processHeartbeat(const std::string &nodeIdentifier) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    auto it = registeredNodes.find(nodeIdentifier);
    if (it != registeredNodes.end()) {
      it->second.lastHeartbeat = time(nullptr);
      it->second.isAlive = true;
      healthCache_.recordSuccess(nodeIdentifier);
      std::cout << "Heartbeat received from node " << nodeIdentifier
                << std::endl;
    } else {
      std::cout << "Heartbeat from unregistered node " << nodeIdentifier
                << std::endl;
      // Optionally, register the node if it sends a heartbeat but is not in the
      // map For now, we just log it.
    }
  }

  // Check for nodes that have not sent a heartbeat recently
  /**
   * @brief Periodically checks all registered nodes for liveness based on
   * heartbeat timestamps. If a node exceeds `NODE_TIMEOUT_SECONDS` without a
   * heartbeat, it's marked as not alive (`isAlive = false`). If a node is
   * marked as offline, this method triggers the replica redistribution logic
   * for any files that had replicas on the failed node.
   * @note This method should be called periodically by the metaserver's main
   * loop or a dedicated timer thread. It also handles logging for node timeouts
   * and replica redistribution actions. Network communication to instruct nodes
   * for replication is handled via the networking library.
   */
  void checkForDeadNodes() {
    std::lock_guard<std::mutex> lock(metadataMutex);
    time_t currentTime = time(nullptr);
    for (auto &entry : registeredNodes) {
      NodeState cacheState = healthCache_.state(entry.first);
      bool cacheDead = cacheState == NodeState::DEAD;
      if (entry.second.isAlive &&
          ((currentTime - entry.second.lastHeartbeat > NODE_TIMEOUT_SECONDS) ||
           cacheDead)) {
        entry.second.isAlive = false;
        std::string deadNodeID = entry.first;
        std::cout << "Node " << deadNodeID << " timed out. Marked as offline."
                  << std::endl;

        std::cout << "Starting replica redistribution for files on "
                  << deadNodeID << std::endl;
        std::vector<std::pair<std::string, std::string>> tasks;

        // Collect tasks: Iterate through fileMetadata to find files hosted on
        // the dead node
        for (const auto &fileEntry : fileMetadata) {
          const std::string &filename = fileEntry.first;
          const std::vector<std::string> &currentReplicas = fileEntry.second;
          if (std::find(currentReplicas.begin(), currentReplicas.end(),
                        deadNodeID) != currentReplicas.end()) {
            tasks.push_back({filename, deadNodeID});
          }
        }

        // Process tasks: For each file that needs a new replica
        for (const auto &task : tasks) {
          const std::string &filename = task.first;
          const std::string &failedNodeID =
              task.second; // Renamed for clarity within this loop
          std::vector<std::string> &currentReplicas = fileMetadata[filename];

          std::cout << "File " << filename << " needs new replica due to "
                    << failedNodeID << " failure." << std::endl;

          std::string newNodeID = "";
          // Find a new node for replica
          for (const auto &nodeEntry : registeredNodes) {
            const std::string &potentialNodeID = nodeEntry.first;
            if (nodeEntry.second.isAlive && potentialNodeID != failedNodeID &&
                std::find(currentReplicas.begin(), currentReplicas.end(),
                          potentialNodeID) == currentReplicas.end()) {
              newNodeID = potentialNodeID;
              break;
            }
          }

          if (newNodeID.empty()) {
            std::cout << "Warning: Could not find a new live node for "
                      << filename << "." << std::endl;
            continue; // Skip to next task
          }

          std::string sourceNodeID = "";
          // Find a live source node from the remaining replicas
          for (const std::string &replicaNodeID : currentReplicas) {
            if (replicaNodeID != failedNodeID &&
                registeredNodes[replicaNodeID].isAlive) {
              sourceNodeID = replicaNodeID;
              break;
            }
          }

          if (sourceNodeID.empty()) {
            std::cout << "Error: No live source replica found for " << filename
                      << "." << std::endl;
            continue; // Skip to next task
          }

          // Update metadata
          currentReplicas.erase(std::remove(currentReplicas.begin(),
                                            currentReplicas.end(),
                                            failedNodeID),
                                currentReplicas.end());
          currentReplicas.push_back(newNodeID);
          std::cout << "Replaced " << failedNodeID << " with " << newNodeID
                    << " for file " << filename << "." << std::endl;

          Message replicateMsg;
          replicateMsg._Type = MessageType::ReplicateFileCommand;
          replicateMsg._Filename = filename;
          replicateMsg._NodeAddress =
              registeredNodes[newNodeID].nodeAddress; // Target for replica
          replicateMsg._Content = sourceNodeID;

          std::string srcAddr = registeredNodes[sourceNodeID].nodeAddress;
          std::string srcIp = srcAddr.substr(0, srcAddr.find(':'));
          int srcPort = std::stoi(srcAddr.substr(srcAddr.find(':') + 1));
          try {
            Networking::Client c(srcIp.c_str(), srcPort);
            c.Send(Message::Serialize(replicateMsg).c_str());
            (void)c.Receive();
            c.Disconnect();
            healthCache_.recordSuccess(sourceNodeID);
          } catch (const std::exception &e) {
            healthCache_.recordFailure(sourceNodeID);
            std::cerr << "[MetadataManager] Replicate send failed to "
                      << sourceNodeID << ": " << e.what() << std::endl;
          }

          Message receiveMsg;
          receiveMsg._Type = MessageType::ReceiveFileCommand;
          receiveMsg._Filename = filename;
          receiveMsg._NodeAddress = srcAddr; // Source of replica
          receiveMsg._Content = newNodeID;

          std::string newIp = registeredNodes[newNodeID].nodeAddress.substr(
              0, registeredNodes[newNodeID].nodeAddress.find(':'));
          int newPort = std::stoi(registeredNodes[newNodeID].nodeAddress.substr(
              registeredNodes[newNodeID].nodeAddress.find(':') + 1));
          try {
            Networking::Client c(newIp.c_str(), newPort);
            c.Send(Message::Serialize(receiveMsg).c_str());
            (void)c.Receive();
            c.Disconnect();
            healthCache_.recordSuccess(newNodeID);
          } catch (const std::exception &e) {
            healthCache_.recordFailure(newNodeID);
            std::cerr << "[MetadataManager] Receive send failed to "
                      << newNodeID << ": " << e.what() << std::endl;
          }
        }
        // After processing all redistributions for a dead node.
        // Call saveMetadata here if defined, path constants should be
        // accessible. saveMetadata(FILE_METADATA_PATH, NODE_REGISTRY_PATH); //
        // Path constants need to be accessible
      }
    }
  }

  // Add a new file and associate nodes to store the chunks
  /**
   * @brief Adds a new file to the system and assigns it to storage nodes based
   * on a replication strategy.
   *
   * The method attempts to use `preferredNodes` if provided and they are alive.
   * If not enough preferred nodes are available or none are provided, it
   * selects other live nodes to meet the `DEFAULT_REPLICATION_FACTOR`.
   *
   * After selecting target nodes, it updates `fileMetadata` and sends messages
   * via the networking library to instruct the target nodes to create the file.
   *
   * @param filename The name of the file to add.
   * @param preferredNodes A list of node identifiers suggested to store this
   * file. Can be empty.
   * @note If no live nodes are available, or not enough to meet a minimum (even
   * if less than desired replication factor), the file might not be added, or a
   * warning is logged.
   */
  void addFile(const std::string &filename,
               const std::vector<std::string>
                   &preferredNodes); // Signature will be modified

  // New/modified public methods for FUSE operations
  bool fileExists(const std::string &filename) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    return fileMetadata.count(filename);
  }
  int getFileAttributes(const std::string &filename, uint32_t &mode,
                        uint32_t &uid, uint32_t &gid, uint64_t &size);
  std::vector<std::string> getAllFileNames();
  int checkAccess(const std::string &filename, uint32_t access_mask);
  int openFile(const std::string &filename,
               uint32_t flags); // flags from FUSE open

  // Retrieve the current content hash (CID) for a file
  std::string getFileHash(const std::string &filename) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    auto it = fileHashes.find(filename);
    if (it != fileHashes.end()) {
      return it->second;
    }
    return "";
  }

  // Modified addFile signature and functionality
  int addFile(const std::string &filename,
              const std::vector<std::string> &preferredNodes,
              unsigned int mode);

  /**
   * @brief Reads data from a file stored on the cluster.
   *
   * The method currently returns placeholder data of the requested size but
   * maintains correct metadata semantics (e.g., EOF handling).
   *
   * @param filename     Name of the file to read from.
   * @param offset       Starting byte offset.
   * @param size_to_read Maximum number of bytes to read.
   * @param out_data     Buffer to store the read data.
   * @param out_size_read Actual number of bytes returned in @p out_data.
   * @return 0 on success or an error code on failure.
   */
  int readFileData(const std::string &filename, int64_t offset,
                   uint64_t size_to_read, std::string &out_data,
                   uint64_t &out_size_read);
  /**
   * @brief Writes data to a file and propagates the update to all replicas.
   *
   * The primary node receives the data first and then replicates it to
   * secondary nodes. Metadata such as file size and content hash are updated
   * accordingly.
   *
   * @param filename        Name of the file to modify.
   * @param offset          Byte offset within the file where the write begins.
   * @param data_to_write   Data to be written.
   * @param out_size_written Number of bytes accepted for the write.
   * @return 0 on success or an error code on failure.
   */
  int writeFileData(const std::string &filename, int64_t offset,
                    const std::string &data_to_write,
                    uint64_t &out_size_written);

  /**
   * @brief Truncate or extend a file to the specified size.
   *
   * Updates internal metadata to reflect the new file size.
   *
   * @param filename Name of the file to modify.
   * @param size     Desired size in bytes.
   * @return 0 on success or an error code on failure.
   */
  int truncateFile(const std::string &filename, uint64_t size);

  int renameFileEntry(const std::string &old_filename,
                      const std::string &new_filename);

  // Existing methods (signatures mostly unchanged, but implementations might
  // need review for new members)
  bool
  removeFile(const std::string &filename); // Declaration for bool return type

  // Retrieve metadata for a given file
  /**
   * @brief Retrieves the list of node identifiers that store replicas of a
   * given file.
   * @param filename The name of the file to query.
   * @return A vector of strings, where each string is a node identifier.
   * @throw std::runtime_error if the file is not found in the metadata.
   */
  std::vector<std::string> getFileNodes(const std::string &filename) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (fileMetadata.find(filename) != fileMetadata.end()) {
      return fileMetadata[filename];
    } else {
      throw std::runtime_error("File not found in metadata.");
    }
  }

  /**
   * @brief Apply snapshot deltas reported by a node after reconnecting.
   *
   * Each line in @p delta should be of the form "Added: <file>",
   * "Modified: <file>", or "Deleted: <file>". This method updates
   * metadata to reflect these changes.
   *
   * @param nodeIdentifier The node that generated the delta list.
   * @param delta Newline-separated delta information.
   * @return True if metadata was modified.
   */
  bool applySnapshotDelta(const std::string &nodeIdentifier,
                          const std::string &delta);

  // Remove a file from metadata
  /**
   * @brief Removes a file from the metadata and sends messages to instruct
   * relevant nodes to delete their replicas.
   * @param filename The name of the file to remove.
   * @note If the file is found and removed from metadata, messages of type
   * `DeleteFile` are sent to each node that was storing a replica.
   */
  // The `bool removeFile(const std::string& filename);` declaration is already
  // correctly placed above. The old implementation block is removed here.
  // getFileNodes is defined below, so its separate declaration was removed.

  // --- Methods for asynchronous saving dirty flag ---
  void markDirty() { metadata_is_dirty_ = true; }

  bool isDirty() const { return metadata_is_dirty_.load(); }

  void clearDirty() { metadata_is_dirty_ = false; }

  /**
   * @brief Get the cached health state of a node.
   * @param nodeIdentifier The node's identifier.
   * @return The NodeState tracked in the health cache.
   */
  NodeState getNodeHealthState(const std::string &nodeIdentifier) const {
    return healthCache_.state(nodeIdentifier);
  }

  std::unordered_map<NodeID, NodeHealthCache::StateInfo>
  getHealthSnapshot() const {
    return healthCache_.snapshot();
  }

  NodeHealthCache &healthCache() { return healthCache_; }

  /// ⭐ NEW CODE
  /**
   * @brief Pick up to @p count nodes that are considered alive.
   */
  std::vector<std::string> pickLiveNodes(size_t count);

  /**
   * @brief Checks if a node with the given identifier is registered.
   * @param nodeIdentifier The unique identifier of the node.
   * @return True if the node is registered, false otherwise.
   */
  bool isNodeRegistered(const std::string &nodeIdentifier) { // Removed const
    std::lock_guard<std::mutex> lock(metadataMutex);
    return registeredNodes.count(nodeIdentifier);
  }

  /**
   * @brief Retrieves NodeInfo for a given node identifier.
   * @param nodeIdentifier The unique identifier of the node.
   * @return A copy of NodeInfo if the node is found.
   * @throw std::runtime_error if the node is not found.
   */
  NodeInfo getNodeInfo(const std::string &nodeIdentifier) { // Removed const
    std::lock_guard<std::mutex> lock(metadataMutex);
    auto it = registeredNodes.find(nodeIdentifier);
    if (it != registeredNodes.end()) {
      return it->second;
    }
    throw std::runtime_error("Node not found in getNodeInfo: " +
                             nodeIdentifier);
  }

  // Print all metadata (for debugging)
  /**
   * @brief Prints all current metadata to the console for debugging purposes.
   * Lists all files and the nodes storing their replicas.
   */
  void printMetadata() {
    std::lock_guard<std::mutex> lock(
        metadataMutex); // Should be const if printMetadata is const
    std::cout << "Current Metadata: " << std::endl;
    for (const auto &entry : fileMetadata) {
      std::cout << "File: " << entry.first << " - Nodes: ";
      for (const auto &node : entry.second) {
        std::cout << node << " ";
      }
      std::cout << std::endl;
    }
  }

  /**
   * @brief Saves the current state of metadata to persistence files.
   *
   * The `fileMetadata` file now includes file hash, mode and size in addition
   * to the list of nodes. Each line has the format:
   * `filename|hash|mode|size|node1,node2,...`
   */
  void saveMetadata(const std::string &fileMetadataPath,
                    const std::string &nodeRegistryPath) {
    std::lock_guard<std::mutex> lock(metadataMutex);

    // Save file metadata along with mode and size
    std::ofstream fm_ofs(fileMetadataPath);
    if (fm_ofs.is_open()) {
      for (const auto &entry : fileMetadata) {
        const std::string &filename = entry.first;
        uint32_t mode = fileModes.count(filename) ? fileModes.at(filename) : 0;
        uint64_t size = fileSizes.count(filename) ? fileSizes.at(filename) : 0;
        std::string hash =
            fileHashes.count(filename) ? fileHashes.at(filename) : "";

        fm_ofs << filename << METADATA_SEPARATOR << hash << METADATA_SEPARATOR
               << mode << METADATA_SEPARATOR << size << METADATA_SEPARATOR;

        for (size_t i = 0; i < entry.second.size(); ++i) {
          fm_ofs << entry.second[i]
                 << (i == entry.second.size() - 1
                         ? ""
                         : std::string(1, NODE_LIST_SEPARATOR));
        }
        fm_ofs << std::endl;
      }
      fm_ofs.close();
    } else {
      std::cerr << "Error: Could not open " << fileMetadataPath
                << " for writing." << std::endl;
    }

    // Save registeredNodes
    std::ofstream nr_ofs(nodeRegistryPath);
    if (nr_ofs.is_open()) {
      for (const auto &entry : registeredNodes) {
        nr_ofs << entry.first << METADATA_SEPARATOR // nodeID
               << entry.second.nodeAddress << METADATA_SEPARATOR
               << entry.second.registrationTime << METADATA_SEPARATOR
               << entry.second.lastHeartbeat << METADATA_SEPARATOR
               << entry.second.isAlive << std::endl;
      }
      nr_ofs.close();
    } else {
      std::cerr << "Error: Could not open " << nodeRegistryPath
                << " for writing." << std::endl;
    }
  }

  /**
   * @brief Loads the state of `fileMetadata`, `fileModes`, `fileSizes` and
   *        `registeredNodes` from persistence files.
   *
   * @param fileMetadataPath Path to the file from which file metadata (mode,
   *        size and node list) will be loaded.
   * @param nodeRegistryPath Path to the file from which node registration
   * information will be loaded.
   * @note Clears current in-memory metadata before loading. Uses
   * `METADATA_SEPARATOR` and `NODE_LIST_SEPARATOR` for parsing. Logs errors if
   * files cannot be opened or if parsing fails.
   */
  void loadMetadata(const std::string &fileMetadataPath,
                    const std::string &nodeRegistryPath) {
    std::lock_guard<std::mutex> lock(
        metadataMutex); // metadataMutex should be mutable for lock_guard in
                        // const method Or isNodeRegistered should not be const
                        // if metadataMutex is not mutable. Making metadataMutex
                        // mutable.

    // Load fileMetadata with modes and sizes
    std::ifstream fm_ifs(fileMetadataPath);
    std::string line;
    if (fm_ifs.is_open()) {
      fileMetadata.clear();
      fileModes.clear();
      fileSizes.clear();
      while (std::getline(fm_ifs, line)) {
        std::vector<std::string> parts;
        size_t start = 0;
        size_t pos = line.find(METADATA_SEPARATOR);
        while (pos != std::string::npos) {
          parts.push_back(line.substr(start, pos - start));
          start = pos + 1;
          pos = line.find(METADATA_SEPARATOR, start);
        }
        parts.push_back(line.substr(start));

        if (parts.size() == 5) {
          const std::string &filename = parts[0];
          std::string hash = parts[1];
          uint32_t mode = 0;
          uint64_t size = 0;
          try {
            mode = static_cast<uint32_t>(std::stoul(parts[2]));
          } catch (...) {
          }
          try {
            size = std::stoull(parts[3]);
          } catch (...) {
          }
          std::string nodesStr = parts[4];

          std::vector<std::string> nodes;
          std::stringstream nodes_ss(nodesStr);
          std::string node;
          while (std::getline(nodes_ss, node, NODE_LIST_SEPARATOR)) {
            if (!node.empty())
              nodes.push_back(node);
          }

          if (!filename.empty()) {
            fileMetadata[filename] = nodes;
            fileModes[filename] = mode;
            fileSizes[filename] = size;
            fileHashes[filename] = hash;
          }
        } else if (parts.size() == 4) { // backward compatibility without hash
          const std::string &filename = parts[0];
          uint32_t mode = 0;
          uint64_t size = 0;
          try {
            mode = static_cast<uint32_t>(std::stoul(parts[1]));
          } catch (...) {
          }
          try {
            size = std::stoull(parts[2]);
          } catch (...) {
          }
          std::string nodesStr = parts[3];

          std::vector<std::string> nodes;
          std::stringstream nodes_ss(nodesStr);
          std::string node;
          while (std::getline(nodes_ss, node, NODE_LIST_SEPARATOR)) {
            if (!node.empty())
              nodes.push_back(node);
          }

          if (!filename.empty()) {
            fileMetadata[filename] = nodes;
            fileModes[filename] = mode;
            fileSizes[filename] = size;
            fileHashes[filename] = "";
          }
        } else if (parts.size() == 2) { // very old format
          const std::string &filename = parts[0];
          std::string nodesStr = parts[1];
          std::vector<std::string> nodes;
          std::stringstream nodes_ss(nodesStr);
          std::string node;
          while (std::getline(nodes_ss, node, NODE_LIST_SEPARATOR)) {
            if (!node.empty())
              nodes.push_back(node);
          }
          if (!filename.empty()) {
            fileMetadata[filename] = nodes;
            fileModes[filename] = 0;
            fileSizes[filename] = 0;
            fileHashes[filename] = "";
          }
        }
      }
      fm_ifs.close();
    } else {
      std::cerr << "Info: Could not open " << fileMetadataPath
                << " for reading. Starting fresh or assuming no prior state."
                << std::endl;
    }

    // Load registeredNodes
    std::ifstream nr_ifs(nodeRegistryPath);
    if (nr_ifs.is_open()) {
      registeredNodes.clear();
      while (std::getline(nr_ifs, line)) {
        std::stringstream ss(line);
        std::string nodeID, nodeAddressStr, regTimeStr, lastHbStr, isAliveStr;

        std::getline(ss, nodeID, METADATA_SEPARATOR);
        std::getline(ss, nodeAddressStr, METADATA_SEPARATOR);
        std::getline(ss, regTimeStr, METADATA_SEPARATOR);
        std::getline(ss, lastHbStr, METADATA_SEPARATOR);
        std::getline(ss, isAliveStr);

        if (!nodeID.empty()) {
          NodeInfo info;
          info.nodeAddress = nodeAddressStr;
          try {
            info.registrationTime = std::stol(regTimeStr); // string to long
            info.lastHeartbeat = std::stol(lastHbStr);     // string to long
            info.isAlive = (isAliveStr == "1");
          } catch (const std::invalid_argument &ia) {
            std::cerr << "Error parsing numeric value for node " << nodeID
                      << ": " << ia.what() << std::endl;
            continue; // Skip this record
          }
          registeredNodes[nodeID] = info;
        }
      }
      nr_ifs.close();
    } else {
      std::cerr << "Info: Could not open " << nodeRegistryPath
                << " for reading. Starting fresh or assuming no prior state."
                << std::endl;
    }
  }

  /**
   * @brief Registers a client thread for diagnostic tracking.
   * @param tid Thread id of the client handler.
   */
  void registerClientThread(std::thread::id tid) {
    std::lock_guard<std::mutex> lock(clientMutex);
    activeClients.insert(tid);
  }

  /**
   * @brief Removes a client thread from tracking.
   * @param tid Thread id of the client handler.
   */
  void unregisterClientThread(std::thread::id tid) {
    std::lock_guard<std::mutex> lock(clientMutex);
    activeClients.erase(tid);
  }

  /**
   * @brief Gets the number of active client threads.
   */
  size_t getActiveClientCount() const {
    std::lock_guard<std::mutex> lock(clientMutex);
    return activeClients.size();
  }
};

// Forward declaration for the client connection handler function defined in
// metaserver.cpp This function is used by the main server loop in
// main_metaserver.cpp No, HandleClientConnection is not what I need here. The
// metaserver class itself doesn't seem to have a Networking::Server instance.
// This means the main_metaserver.cpp is responsible for creating both
// Networking::Server and MetadataManager, and then wiring them together.

// For the test, I will create a MetadataManager instance, and a separate
// Networking::Server. The server's handler will interact with the
// MetadataManager.
namespace Networking {
class ClientConnection;
} // namespace Networking
// void HandleClientConnection(Networking::ClientConnection _pClient); // This
// is likely part of main_metaserver.cpp The actual Metaserver class here *is*
// MetadataManager. Let's adjust the plan.

// The MetadataManager class IS the metaserver logic holder.
// The `registerNode` method is part of it.
// The Node object makes a network call. So the test needs a server that
// can receive this call and then call the `registerNode` on the
// `MetadataManager` instance.
