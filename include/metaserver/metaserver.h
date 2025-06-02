/**
 * @file metaserver.h
 * @brief Defines the MetadataManager class and NodeInfo struct for the SimpliDFS system.
 * 
 * The MetadataManager is the central authority for tracking file metadata,
 * node registration, node liveness (via heartbeats), and orchestrating file
 * replication and data persistence.
 */
#include "utilities/filesystem.h" // Included for context, though not directly used in this header
#include "utilities/message.h"    // For Message struct and MessageType enum
#include "utilities/client.h"       // For Networking::Client
#include "utilities/networkexception.h" // For Networking::NetworkException
#include "utilities/logger.h"     // For Logger
#include <vector>
#include <string>
#include <iostream>
#include <ctime> // Required for time(nullptr)
#include <unordered_map> // Required for std::unordered_map
#include <algorithm> // Required for std::find
#include <fstream>   // For std::ofstream, std::ifstream
#include <sstream>   // For std::stringstream
#include <stdexcept> // For std::invalid_argument in std::stol

// Persistence constants
/** @brief Separator character used in metadata persistence files. */
const char METADATA_SEPARATOR = '|';
/** @brief Separator character for lists of nodes in metadata persistence files. */
const char NODE_LIST_SEPARATOR = ',';

/**
 * @brief Holds information about a registered storage node.
 */
struct NodeInfo {
    std::string nodeAddress; ///< Network address of the node (e.g., "ip:port").
    time_t registrationTime; ///< Timestamp of when the node first registered.
    time_t lastHeartbeat;    ///< Timestamp of the last heartbeat received from this node.
    bool isAlive;            ///< Current liveness status of the node (true if responsive, false if timed out).
    // Potentially other info: capacity, load, etc. could be added here.
};

/** @brief Timeout in seconds. If a node doesn't send a heartbeat within this period, it's marked as not alive. */
const int NODE_TIMEOUT_SECONDS = 30; 

/**
 * @brief Manages all metadata for the SimpliDFS system.
 * 
 * This class is responsible for:
 * - Tracking registered storage nodes and their liveness via heartbeats.
 * - Managing file metadata, including which nodes store replicas of each file.
 * - Implementing a replication strategy for file creation and handling node failures.
 * - Persisting its state (file metadata and node registry) to disk and loading it on startup.
 * All public methods are thread-safe.
 */
class MetadataManager {
private:
    // Mutex to protect all shared metadata (fileMetadata, registeredNodes). Critical for all operations.
    std::mutex metadataMutex; ///< Mutex ensuring thread-safe access to internal data structures.
    
    /** @brief Maps filenames to a list of node identifiers that store replicas of the file. */
    std::unordered_map<std::string, std::vector<std::string>> fileMetadata;
    
    /** @brief Maps node identifiers to NodeInfo structs containing details about each registered node. */
    std::unordered_map<std::string, NodeInfo> registeredNodes;

    // New private members for FUSE attributes
    std::unordered_map<std::string, uint32_t> fileModes;
    std::unordered_map<std::string, uint64_t> fileSizes;
    
    /** @brief Default number of replicas to create for each file. */
    static const int DEFAULT_REPLICATION_FACTOR = 3;

public:
    /**
     * @brief Constructs a MetadataManager object.
     * @note Metadata loading from persistence files is typically handled separately after construction (e.g., in main).
     */
    MetadataManager() {
        // loadMetadata is called from metaserver.cpp after instantiation
    }

    /**
     * @brief Registers a new storage node or updates information for an existing one.
     * Initializes the node's registration time and last heartbeat time. Marks the node as alive.
     * @param nodeIdentifier A unique string identifying the node.
     * @param nodeAddr The network address (IP or hostname) of the node.
     * @param nodePrt The port number the node is listening on.
     */
    void registerNode(const std::string& nodeIdentifier, const std::string& nodeAddr, int nodePrt) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        NodeInfo newNodeInfo;
        newNodeInfo.nodeAddress = nodeAddr + ":" + std::to_string(nodePrt);
        newNodeInfo.registrationTime = time(nullptr);
        newNodeInfo.lastHeartbeat = time(nullptr); // Initialize lastHeartbeat
        newNodeInfo.isAlive = true;

        registeredNodes[nodeIdentifier] = newNodeInfo;
        Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Node " + nodeIdentifier + " registered from " + nodeAddr + ":" + std::to_string(nodePrt));
    }

    // Process a heartbeat message from a node
    void processHeartbeat(const std::string& nodeIdentifier) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        auto it = registeredNodes.find(nodeIdentifier);
        if (it != registeredNodes.end()) {
            it->second.lastHeartbeat = time(nullptr);
            it->second.isAlive = true;
            Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Heartbeat received from node " + nodeIdentifier);
        } else {
            Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] Heartbeat from unregistered node " + nodeIdentifier);
            // Optionally, register the node if it sends a heartbeat but is not in the map
            // For now, we just log it.
        }
    }

    /**
     * @brief Retrieves the NodeInfo struct for a specific registered node.
     * @param nodeID The unique identifier of the node.
     * @return The NodeInfo struct if the node is found.
     * @throw std::runtime_error if the nodeID is not found.
     */
    NodeInfo getNodeInfo(const std::string& nodeID) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        auto it = registeredNodes.find(nodeID);
        if (it != registeredNodes.end()) {
            return it->second;
        } else {
            throw std::runtime_error("Node ID not found in registered nodes: " + nodeID);
        }
    }


    /**
     * @brief Retrieves the network address ("ip:port") of a specific registered node.
     * @param nodeID The unique identifier of the node.
     * @return The network address string if the node is found and registered.
     * @throw std::runtime_error if the nodeID is not found in the registered nodes map.
     */
    std::string getNodeAddress(const std::string& nodeID) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        auto it = registeredNodes.find(nodeID);
        if (it != registeredNodes.end()) {
            return it->second.nodeAddress;
        } else {
            throw std::runtime_error("Node ID not found in registered nodes: " + nodeID);
        }
    }

    // Check for nodes that have not sent a heartbeat recently
    /**
     * @brief Periodically checks all registered nodes for liveness based on heartbeat timestamps.
     * If a node exceeds `NODE_TIMEOUT_SECONDS` without a heartbeat, it's marked as not alive (`isAlive = false`).
     * If a node is marked as offline, this method triggers the replica redistribution logic for
     * any files that had replicas on the failed node.
     * @note This method should be called periodically by the metaserver's main loop or a dedicated timer thread.
     *       It also handles logging for node timeouts and replica redistribution actions.
     *       Actual network communication to instruct nodes for replication is stubbed with log messages.
     */
    void checkForDeadNodes() {
        std::lock_guard<std::mutex> lock(metadataMutex);
        time_t currentTime = time(nullptr);
        for (auto& entry : registeredNodes) {
            // Get logger instance once for the loop if possible, or use getLogger() directly
            Logger& logger_cfdn = Logger::getInstance();
            if (entry.second.isAlive && (currentTime - entry.second.lastHeartbeat > NODE_TIMEOUT_SECONDS)) {
                entry.second.isAlive = false;
                std::string deadNodeID = entry.first;
                logger_cfdn.log(LogLevel::WARN, "[MetadataManager::checkForDeadNodes] Node " + deadNodeID + " timed out. Marked as offline.");
                
                logger_cfdn.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] Starting replica redistribution for files on " + deadNodeID);
                std::vector<std::pair<std::string, std::string>> tasks;

                // Collect tasks: Iterate through fileMetadata to find files hosted on the dead node
                for (const auto& fileEntry : fileMetadata) {
                    const std::string& filename = fileEntry.first;
                    const std::vector<std::string>& currentReplicas = fileEntry.second;
                    if (std::find(currentReplicas.begin(), currentReplicas.end(), deadNodeID) != currentReplicas.end()) {
                        tasks.push_back({filename, deadNodeID});
                    }
                }

                // Process tasks: For each file that needs a new replica
                for (const auto& task : tasks) {
                    const std::string& filename = task.first;
                    const std::string& failedNodeID = task.second; // Renamed for clarity within this loop
                    std::vector<std::string>& currentReplicas = fileMetadata[filename];

                    logger_cfdn.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] File " + filename + " needs new replica due to " + failedNodeID + " failure.");

                    std::string newNodeID = "";
                    // Find a new node for replica
                    for (const auto& nodeEntry : registeredNodes) {
                        const std::string& potentialNodeID = nodeEntry.first;
                        if (nodeEntry.second.isAlive &&
                            potentialNodeID != failedNodeID &&
                            std::find(currentReplicas.begin(), currentReplicas.end(), potentialNodeID) == currentReplicas.end()) {
                            newNodeID = potentialNodeID;
                            break;
                        }
                    }

                    if (newNodeID.empty()) {
                        logger_cfdn.log(LogLevel::WARN, "[MetadataManager::checkForDeadNodes] Warning: Could not find a new live node for " + filename + " after failure of " + failedNodeID);
                        continue; // Skip to next task
                    }

                    std::string sourceNodeID = "";
                    // Find a live source node from the remaining replicas
                    for (const std::string& replicaNodeID : currentReplicas) {
                        if (replicaNodeID != failedNodeID && registeredNodes[replicaNodeID].isAlive) {
                            sourceNodeID = replicaNodeID;
                            break;
                        }
                    }

                    if (sourceNodeID.empty()) {
                        logger_cfdn.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] Error: No live source replica found for " + filename + " after failure of " + failedNodeID);
                        continue; // Skip to next task
                    }

                    // Update metadata
                    currentReplicas.erase(std::remove(currentReplicas.begin(), currentReplicas.end(), failedNodeID), currentReplicas.end());
                    currentReplicas.push_back(newNodeID);
                    logger_cfdn.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] Replaced " + failedNodeID + " with " + newNodeID + " for file " + filename);

                    // Actual network communication for file replication uses 'logger' alias from previous change which is fine.
                    // Logger& logger = Logger::getInstance(); // This line is redundant if logger_cfdn is used or logger is already defined

                    Message replicateMsg;
                    replicateMsg._Type = MessageType::ReplicateFileCommand;
                    replicateMsg._Filename = filename;
                    replicateMsg._NodeAddress = registeredNodes[newNodeID].nodeAddress; // Target for replica
                    replicateMsg._Content = sourceNodeID; // Informing who is the source
                    std::string serRepMsg = Message::Serialize(replicateMsg);

                    // Send ReplicateFileCommand to source node
                    try {
                        std::string sourceNodeAddrStr = registeredNodes[sourceNodeID].nodeAddress;
                        size_t colon_pos_source = sourceNodeAddrStr.find(':');
                        if (colon_pos_source == std::string::npos) {
                            logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] Invalid address format for source node " + sourceNodeID + ": " + sourceNodeAddrStr);
                        } else {
                            std::string sourceIP = sourceNodeAddrStr.substr(0, colon_pos_source);
                            int sourcePort = std::stoi(sourceNodeAddrStr.substr(colon_pos_source + 1));

                            Networking::Client sourceClient(sourceIP, sourcePort);
                            logger.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] Connecting to source node " + sourceNodeID + " at " + sourceIP + ":" + std::to_string(sourcePort) + " to send ReplicateFileCommand for " + filename);
                            sourceClient.Connect();
                            sourceClient.Send(serRepMsg.c_str());
                            sourceClient.Disconnect();
                            logger.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] ReplicateFileCommand sent to source node " + sourceNodeID + " for file " + filename);
                        }
                    } catch (const Networking::NetworkException& ne) {
                        logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] NetworkException while sending ReplicateFileCommand to source node " + sourceNodeID + " for file " + filename + ": " + std::string(ne.what()));
                    } catch (const std::exception& e) { // Catch other potential errors like std::stoi
                        logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] Std::exception while preparing to send ReplicateFileCommand to source node " + sourceNodeID + " for file " + filename + ": " + std::string(e.what()));
                    }

                    Message receiveMsg;
                    receiveMsg._Type = MessageType::ReceiveFileCommand;
                    receiveMsg._Filename = filename;
                    receiveMsg._NodeAddress = registeredNodes[sourceNodeID].nodeAddress; // Source of replica
                    receiveMsg._Content = newNodeID; // Informing who is the target
                    std::string serRecMsg = Message::Serialize(receiveMsg);

                    // Send ReceiveFileCommand to new/target node
                    try {
                        std::string targetNodeAddrStr = registeredNodes[newNodeID].nodeAddress;
                        size_t colon_pos_target = targetNodeAddrStr.find(':');
                        if (colon_pos_target == std::string::npos) {
                            logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] Invalid address format for target node " + newNodeID + ": " + targetNodeAddrStr);
                        } else {
                            std::string targetIP = targetNodeAddrStr.substr(0, colon_pos_target);
                            int targetPort = std::stoi(targetNodeAddrStr.substr(colon_pos_target + 1));

                            Networking::Client targetClient(targetIP, targetPort);
                            logger.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] Connecting to target node " + newNodeID + " at " + targetIP + ":" + std::to_string(targetPort) + " to send ReceiveFileCommand for " + filename);
                            targetClient.Connect();
                            targetClient.Send(serRecMsg.c_str());
                            targetClient.Disconnect();
                            logger.log(LogLevel::INFO, "[MetadataManager::checkForDeadNodes] ReceiveFileCommand sent to target node " + newNodeID + " for file " + filename);
                        }
                    } catch (const Networking::NetworkException& ne) {
                        logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] NetworkException while sending ReceiveFileCommand to target node " + newNodeID + " for file " + filename + ": " + std::string(ne.what()));
                    } catch (const std::exception& e) { // Catch other potential errors like std::stoi
                        logger.log(LogLevel::ERROR, "[MetadataManager::checkForDeadNodes] Std::exception while preparing to send ReceiveFileCommand to target node " + newNodeID + " for file " + filename + ": " + std::string(e.what()));
                    }
                }
                // After processing all redistributions for a dead node.
                // Call saveMetadata here if defined, path constants should be accessible.
                // saveMetadata(FILE_METADATA_PATH, NODE_REGISTRY_PATH); // Path constants need to be accessible
            }
        }
    }

    // Add a new file and associate nodes to store the chunks
    /**
     * @brief Adds a new file to the system and assigns it to storage nodes based on a replication strategy.
     * 
     * The method attempts to use `preferredNodes` if provided and they are alive. 
     * If not enough preferred nodes are available or none are provided, it selects other live nodes
     * to meet the `DEFAULT_REPLICATION_FACTOR`.
     * 
     * After selecting target nodes, it updates `fileMetadata` and logs (stubbed) messages
     * to be sent to the target nodes to create the file.
     * 
     * @param filename The name of the file to add.
     * @param preferredNodes A list of node identifiers suggested to store this file. Can be empty.
     * @note If no live nodes are available, or not enough to meet a minimum (even if less than desired replication factor),
     *       the file might not be added, or a warning is logged.
     */
    void addFile(const std::string &filename, const std::vector<std::string> &preferredNodes); // Signature will be modified

    // New/modified public methods for FUSE operations
    bool fileExists(const std::string& filename) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        return fileMetadata.count(filename);
    }
    int getFileAttributes(const std::string& filename, uint32_t& mode, uint32_t& uid, uint32_t& gid, uint64_t& size);
    std::vector<std::string> getAllFileNames();
    int checkAccess(const std::string& filename, uint32_t access_mask);
    int openFile(const std::string& filename, uint32_t flags); // flags from FUSE open

    // Modified addFile signature and functionality
    int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, unsigned int mode);

    int readFileData(const std::string& filename, int64_t offset, uint64_t size_to_read, std::string& out_data, uint64_t& out_size_read);
    int writeFileData(const std::string& filename, int64_t offset, const std::string& data_to_write, uint64_t& out_size_written);

    int renameFileEntry(const std::string& old_filename, const std::string& new_filename);

    // Existing methods (signatures mostly unchanged, but implementations might need review for new members)
    bool removeFile(const std::string& filename);     // Declaration for bool return type

    int updateFileSizePostWrite(const std::string& filename, int64_t offset, uint64_t bytes_written) {
        std::lock_guard<std::mutex> lock(metadataMutex);
        if (!fileMetadata.count(filename)) {
            Logger::getInstance().log(LogLevel::ERROR, "[MetadataManager] updateFileSizePostWrite: File not found: " + filename);
            return ENOENT;
        }

        uint64_t new_potential_size = static_cast<uint64_t>(offset) + bytes_written;

        // Update file size if this write extends the file or if it's the first write setting the size.
        // This logic is similar to what's in writeFileData, but more direct for size update.
        if (!fileSizes.count(filename) || new_potential_size > fileSizes.at(filename)) {
            fileSizes[filename] = new_potential_size;
            Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] updateFileSizePostWrite: File " + filename + " size updated to " + std::to_string(fileSizes[filename]));
        } else {
            // If writing within existing boundaries, size might not change, but log for debug if needed.
            Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] updateFileSizePostWrite: File " + filename + " write within existing size. Current size: " + std::to_string(fileSizes[filename]) + ", write end offset: " + std::to_string(new_potential_size));
        }
        // This method primarily updates metadata; actual data is on nodes.
        // No direct data involved here, just confirming size based on FUSE write.
        return 0; // Success
    }

    // Retrieve metadata for a given file
    /**
     * @brief Retrieves the list of node identifiers that store replicas of a given file.
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

    // Remove a file from metadata
    /**
     * @brief Removes a file from the metadata and logs (stubbed) messages to instruct relevant nodes to delete their replicas.
     * @param filename The name of the file to remove.
     * @note If the file is found and removed from metadata, messages of type `DeleteFile` are
     *       logged for each node that was storing a replica.
     */
    // The `bool removeFile(const std::string& filename);` declaration is already correctly placed above.
    // The old implementation block is removed here.
    // getFileNodes is defined below, so its separate declaration was removed.

    // Print all metadata (for debugging)
    /**
     * @brief Prints all current metadata to the console for debugging purposes.
     * Lists all files and the nodes storing their replicas.
     */
    void printMetadata() {
        std::lock_guard<std::mutex> lock(metadataMutex);
        Logger& logger_pm = Logger::getInstance();
        logger_pm.log(LogLevel::DEBUG, "[MetadataManager] Current Metadata:");
        for (const auto &entry : fileMetadata) {
            std::string nodeListStr;
            for (size_t i = 0; i < entry.second.size(); ++i) {
                nodeListStr += entry.second[i];
                if (i < entry.second.size() - 1) {
                    nodeListStr += ", ";
                }
            }
            logger_pm.log(LogLevel::DEBUG, "[MetadataManager] File: " + entry.first + " - Nodes: " + nodeListStr);
        }
    }

    /**
     * @brief Saves the current state of `fileMetadata` and `registeredNodes` to persistence files.
     * @param fileMetadataPath Path to the file where file-to-node mappings will be stored.
     * @param nodeRegistryPath Path to the file where node registration information will be stored.
     * @note Uses `METADATA_SEPARATOR` and `NODE_LIST_SEPARATOR` for formatting.
     *       Logs errors if files cannot be opened for writing.
     */
    void saveMetadata(const std::string& fileMetadataPath, const std::string& nodeRegistryPath) {
        std::lock_guard<std::mutex> lock(metadataMutex);

        // Save fileMetadata
        std::ofstream fm_ofs(fileMetadataPath);
        if (fm_ofs.is_open()) {
            for (const auto& entry : fileMetadata) {
                fm_ofs << entry.first << METADATA_SEPARATOR;
                for (size_t i = 0; i < entry.second.size(); ++i) {
                    fm_ofs << entry.second[i] << (i == entry.second.size() - 1 ? "" : std::string(1, NODE_LIST_SEPARATOR));
                }
                fm_ofs << std::endl;
            }
            fm_ofs.close();
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "[MetadataManager] Error: Could not open " + fileMetadataPath + " for writing.");
        }

        // Save registeredNodes
        std::ofstream nr_ofs(nodeRegistryPath);
        if (nr_ofs.is_open()) {
            for (const auto& entry : registeredNodes) {
                nr_ofs << entry.first << METADATA_SEPARATOR                               // nodeID
                       << entry.second.nodeAddress << METADATA_SEPARATOR
                       << entry.second.registrationTime << METADATA_SEPARATOR
                       << entry.second.lastHeartbeat << METADATA_SEPARATOR
                       << entry.second.isAlive << std::endl;
            }
            nr_ofs.close();
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "[MetadataManager] Error: Could not open " + nodeRegistryPath + " for writing.");
        }
    }

    /**
     * @brief Loads the state of `fileMetadata` and `registeredNodes` from persistence files.
     * @param fileMetadataPath Path to the file from which file-to-node mappings will be loaded.
     * @param nodeRegistryPath Path to the file from which node registration information will be loaded.
     * @note Clears current in-memory metadata before loading. Uses `METADATA_SEPARATOR` and 
     *       `NODE_LIST_SEPARATOR` for parsing. Logs errors if files cannot be opened or if parsing fails.
     */
    void loadMetadata(const std::string& fileMetadataPath, const std::string& nodeRegistryPath) {
        std::lock_guard<std::mutex> lock(metadataMutex);

        // Load fileMetadata
        std::ifstream fm_ifs(fileMetadataPath);
        std::string line;
        if (fm_ifs.is_open()) {
            fileMetadata.clear();
            while (std::getline(fm_ifs, line)) {
                std::stringstream ss(line);
                std::string filename, nodesStr;
                std::getline(ss, filename, METADATA_SEPARATOR);
                std::getline(ss, nodesStr); // The rest is node list

                std::vector<std::string> nodes;
                std::stringstream nodes_ss(nodesStr);
                std::string node;
                while(std::getline(nodes_ss, node, NODE_LIST_SEPARATOR)) {
                    nodes.push_back(node);
                }
                if (!filename.empty()) {
                    fileMetadata[filename] = nodes;
                }
            }
            fm_ifs.close();
        } else {
            Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Info: Could not open " + fileMetadataPath + " for reading. Starting fresh or assuming no prior state.");
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
                    } catch (const std::invalid_argument& ia) {
                        Logger::getInstance().log(LogLevel::ERROR, "[MetadataManager] Error parsing numeric value for node " + nodeID + " from registry: " + ia.what());
                        continue; // Skip this record
                    }
                    registeredNodes[nodeID] = info;
                }
            }
            nr_ifs.close();
        } else {
            Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Info: Could not open " + nodeRegistryPath + " for reading. Starting fresh or assuming no prior state.");
        }
    }
};

// Forward declaration for the client connection handler function defined in metaserver.cpp
// This function is used by the main server loop in main_metaserver.cpp
namespace Networking { class ClientConnection; } // Forward declare ClientConnection if not already via includes
void HandleClientConnection(Networking::ClientConnection _pClient);