// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "utilities/filesystem.h" // For FileSystem, though not directly used by MM itself for FUSE ops
#include "utilities/message.h"    // Including message for node communication
#include <sstream>
#include "metaserver/metaserver.h"
#include "utilities/server.h"
#include "utilities/client.h"
#include "utilities/networkexception.h"
#include "utilities/blockio.hpp"
#include "utilities/raft.h"
#include <thread>
#include <chrono>
#include <string>        // Required for std::string, std::to_string
#include "utilities/logger.h" // Include the Logger header
#include <sys/stat.h>    // For S_IFDIR, S_IFREG modes
#include <cerrno>        // For ENOENT, EACCES etc.
#include <algorithm>     // For std::min, std::remove
#include <fcntl.h>       // For open flags like O_RDONLY, O_WRONLY

// Define persistence file paths and separators (already in metaserver.h)

// Networking::Server server(50505); // Global server instance REMOVED
MetadataManager metadataManager;  // Global metadata manager instance
std::unique_ptr<RaftNode> gRaftNode; // Raft instance for leader election

// --- MetadataManager Method Implementations ---

bool MetadataManager::waitForFileMetadata(const std::string& filename,
                                           int retries,
                                           int delay_ms) {
    for (int i = 0; i <= retries; ++i) {
        {
            std::lock_guard<std::mutex> lock(metadataMutex);
            if (fileMetadata.count(filename)) return true;
        }
        if (i < retries) {
            Logger::getInstance().log(LogLevel::DEBUG,
                "[MetadataManager] waitForFileMetadata retry " +
                std::to_string(i+1) + " for " + filename);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    return false;
}

// Constructor, registerNode, processHeartbeat, checkForDeadNodes, printMetadata, saveMetadata, loadMetadata
// are assumed to be already implemented (or their stubs are sufficient for now).
// We will focus on implementing the new/modified methods for FUSE operations.

// Modified addFile to include mode and return an error code
int MetadataManager::addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, unsigned int mode) {
    std::vector<std::string> targetNodes;
    std::vector<std::string> targetAddrs;
    std::unique_lock<std::mutex> lock(metadataMutex);
    if (fileMetadata.count(filename)) {
        Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] addFile: File already exists: " + filename);
        return EEXIST;
    }

    for (const auto& nodeID : preferredNodes) {
        if (targetNodes.size() >= DEFAULT_REPLICATION_FACTOR) break;
        auto it = registeredNodes.find(nodeID);
        if (it != registeredNodes.end() && it->second.isAlive &&
            !healthTracker_.isNodeDead(nodeID) &&
            healthCache_.state(nodeID) == NodeState::ALIVE) {
            if (std::find(targetNodes.begin(), targetNodes.end(), nodeID) == targetNodes.end()) {
                targetNodes.push_back(nodeID);
                targetAddrs.push_back(it->second.nodeAddress);
            }
        }
    }

    if (targetNodes.size() < DEFAULT_REPLICATION_FACTOR) {
        for (const auto& entry : registeredNodes) {
            if (targetNodes.size() >= DEFAULT_REPLICATION_FACTOR) break;
            const std::string& nodeID = entry.first;
            if (entry.second.isAlive && !healthTracker_.isNodeDead(nodeID) &&
                healthCache_.state(nodeID) == NodeState::ALIVE) {
                if (std::find(targetNodes.begin(), targetNodes.end(), nodeID) == targetNodes.end()) {
                    targetNodes.push_back(nodeID);
                    targetAddrs.push_back(entry.second.nodeAddress);
                }
            }
        }
    }

    lock.unlock();

    std::vector<std::string> successes;
    std::vector<std::string> successAddrs;
    for (size_t i = 0; i < targetNodes.size(); ++i) {
        const std::string& nodeID = targetNodes[i];
        const std::string& addr = targetAddrs[i];
        std::string ip = addr.substr(0, addr.find(':'));
        int port = std::stoi(addr.substr(addr.find(':') + 1));

        Message createMsg;
        createMsg._Type = MessageType::WriteFile;
        createMsg._Filename = filename;
        createMsg._Content = "";

        try {
            Networking::Client client(ip.c_str(), port);
            client.Send(Message::Serialize(createMsg).c_str());
            (void)client.Receive();
            client.Disconnect();
            healthTracker_.recordSuccess(nodeID);
            healthCache_.recordSuccess(nodeID);
            successes.push_back(nodeID);
            successAddrs.push_back(addr);
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR,
                "[MetadataManager] Failed to send create command to node " + nodeID + ": " + e.what());
            healthCache_.recordFailure(nodeID);
        }
    }

    if (successes.empty()) {
        Logger::getInstance().log(LogLevel::ERROR, "addFile failed: zero replicas created");
        return ERR_NO_REPLICA;
    }

    if (successes.size() < DEFAULT_REPLICATION_FACTOR) {
        Logger::getInstance().log(LogLevel::WARN, "addFile partial success for " + filename);
        Message delMsg; delMsg._Type = MessageType::DeleteFile; delMsg._Filename = filename;
        for (size_t i = 0; i < successes.size(); ++i) {
            const std::string& nodeID = successes[i];
            const std::string& addr = successAddrs[i];
            std::string ip = addr.substr(0, addr.find(':'));
            int port = std::stoi(addr.substr(addr.find(':') + 1));
            try {
                Networking::Client client(ip.c_str(), port);
                client.Send(Message::Serialize(delMsg).c_str());
                (void)client.Receive();
                client.Disconnect();
                healthTracker_.recordSuccess(nodeID);
                healthCache_.recordSuccess(nodeID);
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR,
                    "[MetadataManager] Failed rollback delete to node " + nodeID + ": " + e.what());
                healthCache_.recordFailure(nodeID);
            }
        }
        return ERR_INSUFFICIENT_REPLICA;
    }

    lock.lock();
    fileMetadata[filename] = successes;
    fileModes[filename] = mode;
    fileSizes[filename] = 0;
    {
        BlockIO bio;
        DigestResult dr = bio.finalize_hashed();
        fileHashes[filename] = dr.cid;
    }
    lock.unlock();

    std::ostringstream oss_add;
    oss_add << "[MetadataManager] File " << filename << " added with mode " << std::oct << mode << std::dec << ". Assigned to nodes: ";
    for(const auto& n : successes) oss_add << n << ' ';
    Logger::getInstance().log(LogLevel::INFO, oss_add.str());

    if (raftNode_) {
        std::ostringstream cmd;
        cmd << "ADD|" << filename << "|" << mode;
        raftNode_->appendCommand(cmd.str());
    }

    return 0;
}

// Modified removeFile to update new maps and return bool
bool MetadataManager::removeFile(const std::string& filename) {
    std::vector<std::string> nodesToNotify;
    std::vector<std::string> nodeAddrs;
    std::unique_lock<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(filename)) {
        Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] removeFile: File not found: " + filename);
        return false; // Indicate file not found or already removed
    }

    nodesToNotify = fileMetadata[filename];
    for (const auto& nodeID : nodesToNotify) {
        auto it = registeredNodes.find(nodeID);
        if (it != registeredNodes.end()) {
            nodeAddrs.push_back(it->second.nodeAddress);
        } else {
            nodeAddrs.push_back("");
        }
    }

    fileMetadata.erase(filename);
    fileModes.erase(filename);
    fileSizes.erase(filename);
    fileHashes.erase(filename);

    Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] File " + filename + " removed from metadata.");
    lock.unlock();

    // Notify each node to remove its replica of the file
    Message delMsg;
    delMsg._Type = MessageType::DeleteFile;
    delMsg._Filename = filename;

    for (size_t i = 0; i < nodesToNotify.size(); ++i) {
        const std::string& nodeID = nodesToNotify[i];
        const std::string& addr = nodeAddrs[i];
        if (addr.empty()) continue;
        std::string ip = addr.substr(0, addr.find(':'));
        int port = std::stoi(addr.substr(addr.find(':') + 1));

        try {
            Networking::Client client(ip.c_str(), port);
            client.Send(Message::Serialize(delMsg).c_str());
            (void)client.Receive();
            client.Disconnect();
            healthTracker_.recordSuccess(nodeID);
            healthCache_.recordSuccess(nodeID);
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR,
                "[MetadataManager] Failed to send delete command to node " + nodeID + ": " + e.what());
            healthCache_.recordFailure(nodeID);
        }
    }
    if (raftNode_) {
        raftNode_->appendCommand("DEL|" + filename);
    }
    return true; // Success
}


int MetadataManager::getFileAttributes(const std::string& filename, uint32_t& mode, uint32_t& uid, uint32_t& gid, uint64_t& size) {
    if (!waitForFileMetadata(filename)) {
        return ENOENT;
    }
    std::lock_guard<std::mutex> lock(metadataMutex);
    mode = fileModes.count(filename) ? fileModes.at(filename) : (S_IFREG | 0644); // Default if not in map
    size = fileSizes.count(filename) ? fileSizes.at(filename) : 0;             // Default if not in map
    uid = 0; // Placeholder UID, SimpliDFS doesn't manage users yet
    gid = 0; // Placeholder GID, SimpliDFS doesn't manage groups yet
    std::ostringstream oss_attr;
    oss_attr << "[MetadataManager] getFileAttributes for " << filename << ": mode=" << std::oct << mode << std::dec << ", size=" << size;
    Logger::getInstance().log(LogLevel::DEBUG, oss_attr.str());
    return 0; // Success
}

std::vector<std::string> MetadataManager::getAllFileNames() {
    std::lock_guard<std::mutex> lock(metadataMutex);
    std::vector<std::string> names;
    names.reserve(fileMetadata.size());
    for (const auto& pair : fileMetadata) {
        names.push_back(pair.first);
    }
    return names;
}

int MetadataManager::checkAccess(const std::string& filename, uint32_t access_mask) {
    if (!waitForFileMetadata(filename)) {
        return ENOENT;
    }
    std::lock_guard<std::mutex> lock(metadataMutex);

    uint32_t mode = fileModes.count(filename) ? fileModes.at(filename) : (S_IFREG | 0644);
    uint32_t perms = mode & 0777; // only permission bits

    if ((access_mask & R_OK) && !(perms & (S_IRUSR | S_IRGRP | S_IROTH))) {
        return EACCES;
    }
    if ((access_mask & W_OK) && !(perms & (S_IWUSR | S_IWGRP | S_IWOTH))) {
        return EACCES;
    }
    if ((access_mask & X_OK) && !(perms & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        return EACCES;
    }

    Logger::getInstance().log(LogLevel::DEBUG,
        "[MetadataManager] checkAccess for " + filename + " (mask: " + std::to_string(access_mask) + ") passed.");
    return 0;
}

int MetadataManager::openFile(const std::string& filename, uint32_t flags) {
    if (!waitForFileMetadata(filename)) {
        return ENOENT;
    }
    std::lock_guard<std::mutex> lock(metadataMutex);

    // O_EXCL without O_CREAT is invalid per POSIX
    if ((flags & O_EXCL) && !(flags & O_CREAT)) {
        return EINVAL;
    }

    uint32_t mode = fileModes.count(filename) ? fileModes.at(filename) : (S_IFREG | 0644);
    uint32_t perms = mode & 0777;

    bool writeRequested = (flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC);
    bool readRequested = !(flags & O_WRONLY); // read for RDONLY or RDWR

    if (writeRequested && !(perms & (S_IWUSR | S_IWGRP | S_IWOTH))) {
        return EACCES;
    }
    if (readRequested && !(perms & (S_IRUSR | S_IRGRP | S_IROTH))) {
        return EACCES;
    }

    if (flags & O_TRUNC) {
        fileSizes[filename] = 0;
    }

    Logger::getInstance().log(LogLevel::DEBUG,
        "[MetadataManager] openFile for " + filename + " (flags: " + std::to_string(flags) + ") succeeded.");
    return 0;
}

int MetadataManager::readFileData(const std::string& filename, int64_t offset, uint64_t size_to_read, std::string& out_data, uint64_t& out_size_read) {
    if (!waitForFileMetadata(filename)) {
        return ENOENT;
    }
    std::lock_guard<std::mutex> lock(metadataMutex);
    Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] readFileData: Actual read from storage node not implemented. Returning placeholder for " + filename);

    // Using stored size for more realistic placeholder behavior
    uint64_t current_file_size = fileSizes.count(filename) ? fileSizes.at(filename) : 0;

    if (offset < 0) offset = 0;
    uint64_t u_offset = static_cast<uint64_t>(offset);

    if (u_offset >= current_file_size) {
        out_data.clear();
        out_size_read = 0;
        return 0; // Read past EOF is not an error, returns 0 bytes
    }

    uint64_t available_len = current_file_size - u_offset;
    out_size_read = std::min(size_to_read, available_len);

    if (out_size_read > 0) {
         // Simulate reading `out_size_read` bytes of character 'D'
         out_data.assign(static_cast<size_t>(out_size_read), 'D');
         Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] readFileData for " + filename + ": returning " + std::to_string(out_size_read) + " placeholder bytes.");
    } else {
        out_data.clear();
         Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] readFileData for " + filename + ": returning 0 bytes (EOF or zero size read).");
    }
    return 0; // Success
}

int MetadataManager::writeFileData(const std::string& filename, int64_t offset, const std::string& data_to_write, uint64_t& out_size_written) {
    if (!waitForFileMetadata(filename)) {
        return ENOENT;
    }

    auto file_lock = getFileWriteLock(filename);
    std::lock_guard<std::mutex> file_guard(*file_lock);

    std::vector<std::pair<std::string, std::string>> nodeAddrs;
    std::unique_lock<std::mutex> lock(metadataMutex);
    Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] writeFileData: Actual write to storage node not implemented for " + filename + ". Updating size only.");

    out_size_written = data_to_write.length();
    if (offset < 0) offset = 0; // Treat negative offset as 0 for this logic

    uint64_t write_end_offset = static_cast<uint64_t>(offset) + out_size_written;

    // Update file size if this write extends the file
    if (!fileSizes.count(filename) || write_end_offset > fileSizes.at(filename)) {
        fileSizes[filename] = write_end_offset;
        Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] File " + filename + " size updated to " + std::to_string(fileSizes[filename]));
        markDirty();
    }

    // Update hash based on this write (simplified: hash only written data)
    {
        BlockIO bio;
        std::vector<std::byte> bytes;
        for(char c : data_to_write) bytes.push_back(std::byte(c));
        if (!bytes.empty()) bio.ingest(bytes.data(), bytes.size());
        DigestResult dr = bio.finalize_hashed();
        fileHashes[filename] = dr.cid;
    }

    uint64_t current_size = fileSizes[filename];
    // Prepare node addresses for network operations then release lock
    const auto nodes = fileMetadata[filename];
    for (const auto& n : nodes) {
        auto it = registeredNodes.find(n);
        if (it != registeredNodes.end()) {
            nodeAddrs.emplace_back(n, it->second.nodeAddress);
        }
    }
    lock.unlock();

    // Send write command to the primary node (first in replica list)
    if (!nodeAddrs.empty()) {
        const std::string& primaryID = nodeAddrs.front().first;
        const std::string& addr = nodeAddrs.front().second;
        std::string ip = addr.substr(0, addr.find(':'));
        int port = std::stoi(addr.substr(addr.find(':') + 1));

            Message writeMsg;
            writeMsg._Type = MessageType::WriteFile;
            writeMsg._Filename = filename;
            writeMsg._Content = data_to_write;
            writeMsg._Offset = offset;

            try {
                Networking::Client client(ip.c_str(), port);
                client.Send(Message::Serialize(writeMsg).c_str());
                (void)client.Receive();
                client.Disconnect();
                healthTracker_.recordSuccess(primaryID);
                healthCache_.recordSuccess(primaryID);
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR,
                    "[MetadataManager] Failed to send write to primary node " + primaryID + ": " + e.what());
                healthCache_.recordFailure(primaryID);
            }

            // Replicate to other nodes
            for (size_t i = 1; i < nodeAddrs.size(); ++i) {
                const std::string& replicaID = nodeAddrs[i].first;
                const std::string& replicaAddr = nodeAddrs[i].second;
                std::string replicaIp = replicaAddr.substr(0, replicaAddr.find(':'));
                int replicaPort = std::stoi(replicaAddr.substr(replicaAddr.find(':') + 1));

                Message replicateMsg;
                replicateMsg._Type = MessageType::ReplicateFileCommand;
                replicateMsg._Filename = filename;
                replicateMsg._NodeAddress = replicaAddr;
                replicateMsg._Content = primaryID;

                Message receiveMsg;
                receiveMsg._Type = MessageType::ReceiveFileCommand;
                receiveMsg._Filename = filename;
                receiveMsg._NodeAddress = addr;
                receiveMsg._Content = replicaID;

                try {
                    Networking::Client clientPrimary(ip.c_str(), port);
                    clientPrimary.Send(Message::Serialize(replicateMsg).c_str());
                    (void)clientPrimary.Receive();
                    clientPrimary.Disconnect();
                    healthTracker_.recordSuccess(primaryID);
                    healthCache_.recordSuccess(primaryID);
                } catch (const std::exception& e) {
                    Logger::getInstance().log(LogLevel::ERROR,
                        "[MetadataManager] Failed to send replicate command to " + primaryID + ": " + e.what());
                    healthCache_.recordFailure(primaryID);
                }

                try {
                    Networking::Client clientReplica(replicaIp.c_str(), replicaPort);
                    clientReplica.Send(Message::Serialize(receiveMsg).c_str());
                    (void)clientReplica.Receive();
                    clientReplica.Disconnect();
                    healthTracker_.recordSuccess(replicaID);
                    healthCache_.recordSuccess(replicaID);
                } catch (const std::exception& e) {
                    Logger::getInstance().log(LogLevel::ERROR,
                        "[MetadataManager] Failed to send receive command to " + replicaID + ": " + e.what());
                    healthCache_.recordFailure(replicaID);
                }
            }
        }

    Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] writeFileData for " + filename + ": " + std::to_string(out_size_written) + " bytes 'written' at offset " + std::to_string(offset) + ". New potential size: " + std::to_string(current_size));
    return 0; // Success
}

int MetadataManager::renameFileEntry(const std::string& old_filename, const std::string& new_filename) {
    if (!waitForFileMetadata(old_filename)) {
        return ENOENT;
    }
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (fileMetadata.count(new_filename)) {
        return EEXIST;
    }

    // Rename in fileMetadata (node list)
    auto node_fh = fileMetadata.extract(old_filename);
    if (node_fh.empty()) return ENOENT; // Should not happen if count check passed
    node_fh.key() = new_filename;
    fileMetadata.insert(std::move(node_fh));

    // Rename in fileModes
    if (fileModes.count(old_filename)) {
        auto mode_fh = fileModes.extract(old_filename);
        if (!mode_fh.empty()) {
            mode_fh.key() = new_filename;
            fileModes.insert(std::move(mode_fh));
        }
    }
    // Rename in fileSizes
    if (fileSizes.count(old_filename)) {
        auto size_fh = fileSizes.extract(old_filename);
        if (!size_fh.empty()) {
            size_fh.key() = new_filename;
            fileSizes.insert(std::move(size_fh));
        }
    }
    if (fileHashes.count(old_filename)) {
        auto hash_fh = fileHashes.extract(old_filename);
        if (!hash_fh.empty()) {
            hash_fh.key() = new_filename;
            fileHashes.insert(std::move(hash_fh));
        }
    }
    Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Renamed " + old_filename + " to " + new_filename);
    if (raftNode_) {
        raftNode_->appendCommand("REN|" + old_filename + "|" + new_filename);
    }
    return 0; // Success
}

// saveMetadata and loadMetadata now persist fileModes and fileSizes alongside
// fileMetadata and registeredNodes. Their definitions reside in the header file.


// --- HandleClientConnection Update ---

// Helper function to normalize FUSE paths
static std::string normalize_path_to_filename(const std::string& fuse_path) {
    if (fuse_path.empty()) {
        return "";
    }
    if (fuse_path.front() == '/') {
        // Skip the leading '/'
        return fuse_path.substr(1);
    }
    return fuse_path;
}

void HandleClientConnection(Networking::Server& server_instance, Networking::ClientConnection _pClient)
{
    std::cerr << "DIAGNOSTIC: HandleClientConnection: Thread started for client " << server_instance.GetClientIPAddress(_pClient) << std::endl;
    std::string client_ip_str = server_instance.GetClientIPAddress(_pClient); // Store for logging after disconnect
    metadataManager.registerClientThread(std::this_thread::get_id());

    while(true) { // Loop to handle multiple requests
        try {
            Logger::getInstance().log(LogLevel::DEBUG,
                "Handling client connection from " + client_ip_str +
                ", active clients: " + std::to_string(metadataManager.getActiveClientCount()));
            std::vector<char> received_vector = server_instance.Receive(_pClient);
            std::cerr << "DIAGNOSTIC: HandleClientConnection: Received " << received_vector.size() << " bytes from client." << std::endl;
            if (received_vector.empty()) {
                Logger::getInstance().log(LogLevel::INFO, "Client " + client_ip_str + " disconnected or receive failed. Closing connection.");
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Thread finishing due to empty receive for client " << client_ip_str << std::endl;
                break;
            }
            std::string received_data_str(received_vector.begin(), received_vector.end());
            Logger::getInstance().log(LogLevel::DEBUG, "Received data from " + client_ip_str + ": " + received_data_str);

            Message request;
            try {
                request = Message::Deserialize(received_data_str);
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Deserialized request type " << static_cast<int>(request._Type) << " for path '" << request._Path << "' from client." << std::endl;
            } catch (const std::runtime_error& re) {
                 Logger::getInstance().log(LogLevel::ERROR, "Failed to deserialize message from " + client_ip_str + ": " + re.what() + ". Closing connection.");
                 break; // Critical error, terminate connection handling
            }

            bool shouldSave = false;
            Message response_to_send; // To hold the message to be sent, avoids serializing multiple times if not needed for some paths.

            switch (request._Type)
            {
                case MessageType::GetAttr:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case GetAttr for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received GetAttr for: " + request._Path);
                Message res_msg;
                res_msg._Type = MessageType::GetAttrResponse;

                if (request._Path == "/") {
                    res_msg._ErrorCode = 0;
                    res_msg._Mode = S_IFDIR | 0755;
                    res_msg._Uid = 0;
                    res_msg._Gid = 0;
                    res_msg._Size = 4096;
                } else {
                    std::string norm_path = normalize_path_to_filename(request._Path);
                    res_msg._ErrorCode = metadataManager.getFileAttributes(norm_path, res_msg._Mode, res_msg._Uid, res_msg._Gid, res_msg._Size);
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case GetAttr, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Readdir:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Readdir for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Readdir for: " + request._Path);
                Message res_msg;
                res_msg._Type = MessageType::ReaddirResponse;
                if (request._Path == "/") {
                    std::vector<std::string> names = metadataManager.getAllFileNames();
                    res_msg._Data.clear();
                    for(const auto& name : names) {
                        res_msg._Data.append(name);
                        res_msg._Data.push_back('\0');
                    }
                    res_msg._ErrorCode = 0;
                } else {
                    res_msg._ErrorCode = ENOTDIR;
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Readdir, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Access:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Access for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Access for: " + request._Path + " with mode " + std::to_string(request._Mode));
                Message res_msg;
                res_msg._Type = MessageType::AccessResponse;

                if (request._Path == "/") {
                    res_msg._ErrorCode = 0;
                } else {
                    std::string norm_path = normalize_path_to_filename(request._Path);
                    res_msg._ErrorCode = metadataManager.checkAccess(norm_path, static_cast<uint32_t>(request._Mode));
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Access, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Open:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Open for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Open for: " + request._Path + " with flags " + std::to_string(request._Mode));
                Message res_msg;
                res_msg._Type = MessageType::OpenResponse;

                if (request._Path == "/") {
                    res_msg._ErrorCode = 0;
                } else {
                     std::string norm_path = normalize_path_to_filename(request._Path);
                    // O_CREAT is typically handled by a separate `create` call from FUSE
                    // This open is more about checking if file exists and can be opened based on flags (RDONLY, WRONLY, etc.)
                    // which checkAccess implicitly does for now.
                    res_msg._ErrorCode = metadataManager.openFile(norm_path, static_cast<uint32_t>(request._Mode));
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Open, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::CreateFile:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case CreateFile for path '" << request._Path << "'" << std::endl;
                std::ostringstream oss_create;
                oss_create << "[Metaserver] Received CreateFile for: " << request._Path << " with mode " << std::oct << request._Mode << std::dec;
                Logger::getInstance().log(LogLevel::INFO, oss_create.str());
                Message res_msg;
                res_msg._Type = MessageType::CreateFileResponse;
                std::string norm_path_filename = normalize_path_to_filename(request._Path);

                if (norm_path_filename == "/" || norm_path_filename.empty() || norm_path_filename.rfind('/') != std::string::npos) {
                    res_msg._ErrorCode = EINVAL;
                } else {
                    // Preferred nodes list is empty for now, can be enhanced later
                    res_msg._ErrorCode = metadataManager.addFile(norm_path_filename, {}, static_cast<uint32_t>(request._Mode));
                    if (res_msg._ErrorCode == 0) {
                        shouldSave = true;
                    }
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case CreateFile, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::ReadFile:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case ReadFile for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Read for: " + request._Path + " Offset: " + std::to_string(request._Offset) + " Size: " + std::to_string(request._Size));
                Message res_msg;
                res_msg._Type = MessageType::ReadResponse;
                std::string norm_path_filename = normalize_path_to_filename(request._Path);

                res_msg._ErrorCode = metadataManager.readFileData(norm_path_filename, request._Offset, request._Size, res_msg._Data, res_msg._Size);
                // readFileData sets res_msg._Size to actual bytes read/to be sent
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case ReadFile, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::WriteFile:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case WriteFile for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Write for: " + request._Path + " Offset: " + std::to_string(request._Offset) + " Size: " + std::to_string(request._Size) + " DataLen: " + std::to_string(request._Data.length()));
                Message res_msg;
                res_msg._Type = MessageType::WriteResponse;
                std::string norm_path_filename = normalize_path_to_filename(request._Path);
                uint64_t bytes_written_confirmed;

                res_msg._ErrorCode = metadataManager.writeFileData(norm_path_filename, request._Offset, request._Data, bytes_written_confirmed);
                if (res_msg._ErrorCode == 0) {
                    res_msg._Size = bytes_written_confirmed; // Set the size in response to what was "written"
                    shouldSave = true;
                } else {
                    res_msg._Size = 0; // Ensure size is 0 on error
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case WriteFile, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Unlink:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Unlink for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Unlink for: " + request._Path);
                Message res_msg;
                res_msg._Type = MessageType::UnlinkResponse;
                std::string norm_path_filename = normalize_path_to_filename(request._Path);

                if (norm_path_filename == "/" || norm_path_filename.empty() || norm_path_filename.rfind('/') != std::string::npos) {
                    res_msg._ErrorCode = EISDIR;
                } else {
                    if (metadataManager.removeFile(norm_path_filename)) {
                        res_msg._ErrorCode = 0;
                        shouldSave = true;
                    } else {
                        res_msg._ErrorCode = ENOENT;
                    }
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Unlink, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Rename:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Rename for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Rename for: " + request._Path + " to " + request._NewPath);
                Message res_msg;
                res_msg._Type = MessageType::RenameResponse;
                std::string norm_old_filename = normalize_path_to_filename(request._Path);
                std::string norm_new_filename = normalize_path_to_filename(request._NewPath);

                bool invalid_paths = norm_old_filename == "/" || norm_old_filename.empty() || norm_old_filename.rfind('/') != std::string::npos ||
                                   norm_new_filename == "/" || norm_new_filename.empty() || norm_new_filename.rfind('/') != std::string::npos;

                if (invalid_paths) {
                    res_msg._ErrorCode = EINVAL;
                } else {
                     res_msg._ErrorCode = metadataManager.renameFileEntry(norm_old_filename, norm_new_filename);
                     if (res_msg._ErrorCode == 0) {
                         shouldSave = true;
                     }
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Rename, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            // Node Management Cases (existing)
            case MessageType::RegisterNode:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case RegisterNode for node '" << request._Filename << "'" << std::endl;
                metadataManager.registerNode(request._Filename, request._NodeAddress, request._NodePort);
                shouldSave = true;
                Message reg_res;
                reg_res._Type = MessageType::RegisterNode;
                reg_res._ErrorCode = 0;
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case RegisterNode, node '" << request._Filename << "'" << std::endl;
                server_instance.Send(Message::Serialize(reg_res).c_str(), _pClient);
                Logger::getInstance().log(LogLevel::INFO, "Sent registration confirmation to node " + request._Filename);
                break;
            }
            case MessageType::GetFileNodeLocationsRequest:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case GetFileNodeLocationsRequest for path '" << request._Path << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received GetFileNodeLocationsRequest for: " + request._Path);
                Message res_msg;
                res_msg._Type = MessageType::GetFileNodeLocationsResponse;
                res_msg._Path = request._Path; // Echo back the path for context

                std::string norm_path = normalize_path_to_filename(request._Path);

                if (norm_path.empty()) {
                    res_msg._ErrorCode = EINVAL; // Invalid argument if path is empty or just "/"
                } else {
                    try {
                        std::vector<std::string> node_ids = metadataManager.getFileNodes(norm_path);
                        if (node_ids.empty()) {
                            // This case might mean file exists but has no nodes (should ideally not happen if addFile ensures nodes)
                            // Or getFileNodes throws if file doesn't exist, so this path might not be hit for "no nodes".
                            Logger::getInstance().log(LogLevel::WARN, "[Metaserver] File " + norm_path + " found but has no associated nodes.");
                            res_msg._ErrorCode = ENOENT; // Or a more specific error like ENODATA
                        } else {
                            std::stringstream ss_node_addresses;
                            for (size_t i = 0; i < node_ids.size(); ++i) {
                                try {
                                    NodeInfo node_info = metadataManager.getNodeInfo(node_ids[i]);
                                    ss_node_addresses << node_info.nodeAddress;
                                    if (i < node_ids.size() - 1) {
                                        ss_node_addresses << ",";
                                    }
                                } catch (const std::runtime_error& re_node) {
                                    // Log that a specific node ID couldn't be resolved, but continue if possible
                                    Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Error getting info for node ID " + node_ids[i] + " for file " + norm_path + ": " + re_node.what());
                                    // Depending on desired behavior, could set an error here or just skip this node
                                }
                            }
                            res_msg._Data = ss_node_addresses.str();
                            if (res_msg._Data.empty() && !node_ids.empty()) {
                                // This means all node_ids lookup failed, which is an error.
                                Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Failed to resolve any node addresses for file " + norm_path);
                                res_msg._ErrorCode = EHOSTUNREACH; // Or some other suitable error
                            } else if (res_msg._Data.empty() && node_ids.empty()){
                                // Explicitly set ENOENT if no nodes were found (e.g. file exists but node list is empty)
                                res_msg._ErrorCode = ENOENT;
                            }
                             else {
                                res_msg._ErrorCode = 0; // Success
                            }
                        }
                    } catch (const std::runtime_error& re_file) {
                        Logger::getInstance().log(LogLevel::WARN, "[Metaserver] GetFileNodeLocationsRequest: File not found or error for " + norm_path + ": " + re_file.what());
                        res_msg._ErrorCode = ENOENT; // File not found
                    }
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case GetFileNodeLocationsRequest, path '" << request._Path << "'" << std::endl;
                server_instance.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Heartbeat:
            {
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Heartbeat for node '" << request._Filename << "'" << std::endl;
                Logger::getInstance().log(LogLevel::DEBUG, "Received Heartbeat from node " + request._Filename);
                metadataManager.processHeartbeat(request._Filename);
                // No response sent for heartbeat
                break;
            }
            case MessageType::DeleteFile: { // Legacy DeleteFile, treat like unlink
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case DeleteFile for path '" << request._Filename << "'" << std::endl;
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received (legacy) DeleteFile request for " + request._Filename);
                Message del_res;
                del_res._Type = MessageType::FileRemoved;
                std::string file_to_delete = normalize_path_to_filename(request._Filename);

                if (file_to_delete == "/" || file_to_delete.empty() || file_to_delete.rfind('/') != std::string::npos) {
                     del_res._ErrorCode = EISDIR;
                } else if (metadataManager.removeFile(file_to_delete)) {
                    del_res._ErrorCode = 0;
                    shouldSave = true;
                } else {
                    del_res._ErrorCode = ENOENT;
                }
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case DeleteFile, path '" << request._Filename << "'" << std::endl;
                server_instance.Send(Message::Serialize(del_res).c_str(), _pClient);
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Sent DeleteFile processing result for " + file_to_delete);
                break;
            }
            case MessageType::RaftRequestVote:
            case MessageType::RaftRequestVoteResponse:
            case MessageType::RaftAppendEntries:
            case MessageType::RaftAppendEntriesResponse:
            {
                if (gRaftNode) {
                    gRaftNode->handleMessage(request, request._NodeAddress);
                }
                break;
            }
            default:
                std::cerr << "DIAGNOSTIC: HandleClientConnection: Processing case Default (Unknown Type) for type " << static_cast<int>(request._Type) << std::endl;
                Logger::getInstance().log(LogLevel::WARN, "Received unhandled or unknown message type: " + std::to_string(static_cast<int>(request._Type)) + " from client " + server_instance.GetClientIPAddress(_pClient));
                Message err_res;
                err_res._Type = request._Type;
                err_res._ErrorCode = ENOSYS;
                std::cerr << "DIAGNOSTIC: HandleClientConnection: About to send response for case Default (Unknown Type)" << std::endl;
                server_instance.Send(Message::Serialize(err_res).c_str(), _pClient);
                break;
        }

            if (shouldSave) {
                Logger::getInstance().log(LogLevel::DEBUG, "[Metaserver] Metadata modified, marking as dirty.");
                metadataManager.markDirty();
            }
        } catch (const Networking::NetworkException& ne) {
            std::cerr << "DIAGNOSTIC: HandleClientConnection: NetworkException caught: " << ne.what() << " for client " << client_ip_str << std::endl;
            Logger::getInstance().log(LogLevel::ERROR, "Network error in HandleClientConnection for " + client_ip_str + ": " + std::string(ne.what()));
            break; // Exit loop on network error
        } catch (const std::runtime_error& re) {
            std::cerr << "DIAGNOSTIC: HandleClientConnection: runtime_error caught: " << re.what() << " for client " << client_ip_str << std::endl;
            Logger::getInstance().log(LogLevel::ERROR, "Runtime error (e.g., deserialization) in HandleClientConnection for " + client_ip_str + ": " + std::string(re.what()));
            break; // Exit loop on runtime error
        } catch (const std::exception& e) {
            std::cerr << "DIAGNOSTIC: HandleClientConnection: std::exception caught: " << e.what() << " for client " << client_ip_str << std::endl;
            Logger::getInstance().log(LogLevel::ERROR, "Generic exception in HandleClientConnection for " + client_ip_str + ": " + std::string(e.what()));
            break; // Exit loop on generic error
        }  catch (...) {
            std::cerr << "DIAGNOSTIC: HandleClientConnection: UNHANDLED EXCEPTION CAUGHT IN THREAD for client " << client_ip_str << ". Thread terminating." << std::endl;
            Logger::getInstance().log(LogLevel::ERROR, "Unknown exception in HandleClientConnection for " + client_ip_str);
            break; // Exit loop on unknown error
        }
    } // End of while(true) loop

    try {
        server_instance.DisconnectClient(_pClient);
        std::cerr << "DIAGNOSTIC: HandleClientConnection: Client " << client_ip_str << " disconnected." << std::endl;
        Logger::getInstance().log(LogLevel::INFO, "Disconnected client " + client_ip_str);
    } catch (const Networking::NetworkException& ne) {
        std::cerr << "DIAGNOSTIC: HandleClientConnection: NetworkException during DisconnectClient for " << client_ip_str << ": " << ne.what() << std::endl;
        Logger::getInstance().log(LogLevel::ERROR, "Network error during DisconnectClient for " + client_ip_str + ": " + std::string(ne.what()));
    }

    metadataManager.unregisterClientThread(std::this_thread::get_id());
    std::cerr << "DIAGNOSTIC: HandleClientConnection: Thread finishing for client " << client_ip_str << std::endl;
}

// ⭐ NEW CODE
std::vector<std::string> MetadataManager::pickLiveNodes(size_t count) {
    std::vector<std::string> healthy = healthCache_.getHealthyNodes();
    std::vector<std::string> result;
    std::lock_guard<std::mutex> lock(metadataMutex);
    for (const auto& id : healthy) {
        auto it = registeredNodes.find(id);
        if (it != registeredNodes.end() && it->second.isAlive && !healthTracker_.isNodeDead(id)) {
            result.push_back(id);
            if (result.size() == count) break;
        }
    }
    return result;
}

// main() function has been moved to src/main_metaserver.cpp
