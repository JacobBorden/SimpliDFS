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
#include "utilities/client.h" // Added for Networking::Client
#include "utilities/networkexception.h"
#include <thread>
#include <string>        // Required for std::string, std::to_string
#include "utilities/logger.h" // Include the Logger header
#include <sys/stat.h>    // For S_IFDIR, S_IFREG modes
#include <cerrno>        // For ENOENT, EACCES etc.
#include <algorithm>     // For std::min, std::remove

// Define persistence file paths and separators (already in metaserver.h)

Networking::Server server(50505); // Global server instance
MetadataManager metadataManager;  // Global metadata manager instance

// --- MetadataManager Method Implementations ---

// Constructor, registerNode, processHeartbeat, checkForDeadNodes, printMetadata, saveMetadata, loadMetadata
// are assumed to be already implemented (or their stubs are sufficient for now).
// We will focus on implementing the new/modified methods for FUSE operations.

// Modified addFile to include mode and return an error code
int MetadataManager::addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, unsigned int mode) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (fileMetadata.count(filename)) {
        Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] addFile: File already exists: " + filename);
        return EEXIST;
    }

    std::vector<std::string> targetNodes;
    // Try to use preferred nodes if they are alive
    for (const auto& nodeID : preferredNodes) {
        if (targetNodes.size() >= DEFAULT_REPLICATION_FACTOR) break;
        auto it = registeredNodes.find(nodeID);
        if (it != registeredNodes.end() && it->second.isAlive) {
            if (std::find(targetNodes.begin(), targetNodes.end(), nodeID) == targetNodes.end()) {
                targetNodes.push_back(nodeID);
            }
        }
    }

    // If not enough nodes from preferred, fill with other alive nodes
    if (targetNodes.size() < DEFAULT_REPLICATION_FACTOR) {
        for (const auto& entry : registeredNodes) {
            if (targetNodes.size() >= DEFAULT_REPLICATION_FACTOR) break;
            const std::string& nodeID = entry.first;
            if (entry.second.isAlive) {
                if (std::find(targetNodes.begin(), targetNodes.end(), nodeID) == targetNodes.end()) {
                    targetNodes.push_back(nodeID);
                }
            }
        }
    }

    if (targetNodes.empty()) {
        Logger::getInstance().log(LogLevel::ERROR, "[MetadataManager] addFile: No live nodes available for file " + filename);
        return ENOSPC; // No space/nodes available
    } else if (targetNodes.size() < DEFAULT_REPLICATION_FACTOR) {
        Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] addFile: Could only find " + std::to_string(targetNodes.size()) +
                                               " live nodes for file " + filename + ". Required: " + std::to_string(DEFAULT_REPLICATION_FACTOR));
    }

    fileMetadata[filename] = targetNodes;
    fileModes[filename] = mode;
    fileSizes[filename] = 0; // Initial size is 0

    std::ostringstream oss_add;
    oss_add << "[MetadataManager] File " << filename << " added with mode " << std::oct << mode << std::dec << ". Assigned to nodes: ";
    for(const auto& n : targetNodes) oss_add << n << " ";
    Logger::getInstance().log(LogLevel::INFO, oss_add.str());


    // TODO: Actual communication to nodes to create file blocks would happen here or be initiated from here.
    // For now, metaserver just records it.
    return 0; // Success
}

// Modified removeFile to update new maps and return bool
bool MetadataManager::removeFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(filename)) {
        Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] removeFile: File not found: " + filename);
        return false; // Indicate file not found or already removed
    }

    std::vector<std::string> nodesToNotify = fileMetadata[filename];

    fileMetadata.erase(filename);
    fileModes.erase(filename);
    fileSizes.erase(filename);

    Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] File " + filename + " removed from metadata.");

    // TODO: Actual communication to nodes to delete file blocks
    Message delMsg;
    delMsg._Type = MessageType::DeleteFile; // Or a more specific internal command
    delMsg._Filename = filename;
    for (const auto& nodeID : nodesToNotify) {
        Logger::getInstance().log(LogLevel::DEBUG, "[Metaserver_STUB] Instructing node " + nodeID + " to delete file " + filename);
        // server.SendToNode(nodeID, Message::Serialize(delMsg)); // Hypothetical send to specific node
    }
    return true; // Success
}


int MetadataManager::getFileAttributes(const std::string& filename, uint32_t& mode, uint32_t& uid, uint32_t& gid, uint64_t& size) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(filename)) {
        return ENOENT;
    }
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
    std::lock_guard<std::mutex> lock(metadataMutex);
    (void)access_mask; // access_mask is not used in this simplified version
    if (!fileMetadata.count(filename)) {
        return ENOENT;
    }
    // TODO: Implement actual permission checking based on stored fileModes[filename] and access_mask.
    // This would involve bitwise operations to see if the requested permissions (R_OK, W_OK, X_OK in mask)
    // are granted by the file's mode.
    Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] checkAccess for " + filename + " (mask: " + std::to_string(access_mask) + "). Optimistically returning success.");
    return 0; // Optimistic: if file exists, access is granted for now.
}

int MetadataManager::openFile(const std::string& filename, uint32_t flags) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    (void)flags; // flags (like O_RDONLY, O_WRONLY, O_RDWR) are not fully utilized yet.
                 // O_CREAT is handled by addFile. O_EXCL would need check here.
    if (!fileMetadata.count(filename)) {
        return ENOENT;
    }
    // TODO: More sophisticated open logic if needed (e.g., check flags like O_EXCL if O_CREAT was also set,
    // though FUSE usually separates create() and open() calls).
    Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] openFile for " + filename + " (flags: " + std::to_string(flags) + "). Optimistically returning success.");
    return 0; // Optimistic: if file exists, open is allowed.
}

int MetadataManager::readFileData(const std::string& filename, int64_t offset, uint64_t size_to_read, std::string& out_data, uint64_t& out_size_read) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(filename)) {
        return ENOENT;
    }
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
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(filename)) {
        return ENOENT;
    }
    Logger::getInstance().log(LogLevel::WARN, "[MetadataManager] writeFileData: Actual write to storage node not implemented for " + filename + ". Updating size only.");

    out_size_written = data_to_write.length();
    if (offset < 0) offset = 0; // Treat negative offset as 0 for this logic

    uint64_t write_end_offset = static_cast<uint64_t>(offset) + out_size_written;

    // Update file size if this write extends the file
    if (!fileSizes.count(filename) || write_end_offset > fileSizes.at(filename)) {
        fileSizes[filename] = write_end_offset;
        Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] File " + filename + " size updated to " + std::to_string(fileSizes[filename]));
    }

    // TODO: Notify storage nodes about the write and data.
    // This would involve selecting primary node, sending data, and handling replication.
    Logger::getInstance().log(LogLevel::DEBUG, "[MetadataManager] writeFileData for " + filename + ": " + std::to_string(out_size_written) + " bytes 'written' at offset " + std::to_string(offset) + ". New potential size: " + std::to_string(fileSizes[filename]));
    return 0; // Success
}

int MetadataManager::renameFileEntry(const std::string& old_filename, const std::string& new_filename) {
    std::lock_guard<std::mutex> lock(metadataMutex);
    if (!fileMetadata.count(old_filename)) {
        return ENOENT;
    }
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
    Logger::getInstance().log(LogLevel::INFO, "[MetadataManager] Renamed " + old_filename + " to " + new_filename);
    return 0; // Success
}

// TODO: Update saveMetadata and loadMetadata to persist fileModes and fileSizes maps.
// Definitions for saveMetadata and loadMetadata are now only in the header file.


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

void HandleClientConnection(Networking::ClientConnection _pClient)
{
    try {
        Logger::getInstance().log(LogLevel::DEBUG, "Handling client connection from " + server.GetClientIPAddress(_pClient));
        std::vector<char> received_vector = server.Receive(_pClient);
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::WARN, "Received empty data from client " + server.GetClientIPAddress(_pClient));
            return; 
        }
        std::string received_data_str(received_vector.begin(), received_vector.end());
        Logger::getInstance().log(LogLevel::DEBUG, "Received data from " + server.GetClientIPAddress(_pClient) + ": " + received_data_str);
        Message request = Message::Deserialize(received_data_str);
        bool shouldSave = false;

        switch (request._Type)
        {
            case MessageType::GetAttr:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::UpdateFileMetadata:
            {
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received UpdateFileMetadata for: " + request._Path + ", Offset: " + std::to_string(request._Offset) + ", Size: " + std::to_string(request._Size));
                Message res_msg;
                res_msg._Type = MessageType::UpdateFileMetadataResponse;
                std::string norm_path = normalize_path_to_filename(request._Path);

                // The _Size field in the request here means bytes_written from FUSE adapter's perspective for this segment.
                // The _Offset is where the write started.
                res_msg._ErrorCode = metadataManager.updateFileSizePostWrite(norm_path, request._Offset, request._Size);

                if (res_msg._ErrorCode == 0) {
                    shouldSave = true; // File metadata (size) changed, persist it.
                    Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Successfully updated metadata for " + norm_path);
                } else {
                    Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Failed to update metadata for " + norm_path + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));
                }
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Readdir:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Access:
            {
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Access for: " + request._Path + " with mode " + std::to_string(request._Mode));
                Message res_msg;
                res_msg._Type = MessageType::AccessResponse;

                if (request._Path == "/") {
                    res_msg._ErrorCode = 0;
                } else {
                    std::string norm_path = normalize_path_to_filename(request._Path);
                    res_msg._ErrorCode = metadataManager.checkAccess(norm_path, static_cast<uint32_t>(request._Mode));
                }
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Open:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::CreateFile:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::ReadFile:
            {
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received Read for: " + request._Path + " Offset: " + std::to_string(request._Offset) + " Size: " + std::to_string(request._Size));
                Message res_msg;
                res_msg._Type = MessageType::ReadResponse;
                std::string norm_path_filename = normalize_path_to_filename(request._Path);

                res_msg._ErrorCode = metadataManager.readFileData(norm_path_filename, request._Offset, request._Size, res_msg._Data, res_msg._Size);
                // readFileData sets res_msg._Size to actual bytes read/to be sent
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::WriteFile:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Unlink:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::Rename:
            {
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
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            // Node Management Cases (existing)
            case MessageType::RegisterNode:
            {
                metadataManager.registerNode(request._Filename, request._NodeAddress, request._NodePort);
                shouldSave = true;
                Message reg_res;
                reg_res._Type = MessageType::RegisterNode;
                reg_res._ErrorCode = 0;
                server.Send(Message::Serialize(reg_res).c_str(), _pClient);
                Logger::getInstance().log(LogLevel::INFO, "Sent registration confirmation to node " + request._Filename);
                break;
            }
            case MessageType::Heartbeat:
            {
                Logger::getInstance().log(LogLevel::DEBUG, "Received Heartbeat from node " + request._Filename);
                metadataManager.processHeartbeat(request._Filename);
                break;
            }
            case MessageType::DeleteFile: { // Legacy DeleteFile, treat like unlink
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
                server.Send(Message::Serialize(del_res).c_str(), _pClient);
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Sent DeleteFile processing result for " + file_to_delete);
                break;
            }
            case MessageType::GetFileNodeLocations:
            {
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received GetFileNodeLocations for: " + request._Path);
                Message res_msg;
                res_msg._Type = MessageType::GetFileNodeLocationsResponse;
                std::string norm_path = normalize_path_to_filename(request._Path);
                try {
                    std::vector<std::string> node_ids = metadataManager.getFileNodes(norm_path);
                    if (node_ids.empty()) {
                        res_msg._ErrorCode = ENOENT; // Or some other error indicating no nodes have this file
                        Logger::getInstance().log(LogLevel::WARN, "[Metaserver] No nodes found for file: " + norm_path);
                    } else {
                        std::string addresses_str;
                        for (size_t i = 0; i < node_ids.size(); ++i) {
                            try {
                                addresses_str += metadataManager.getNodeAddress(node_ids[i]);
                                if (i < node_ids.size() - 1) {
                                    addresses_str += ",";
                                }
                            } catch (const std::runtime_error& e_node) {
                                // Log error if a specific node ID isn't found in registeredNodes (should be rare if metadata is consistent)
                                Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Error getting address for node ID " + node_ids[i] + " for file " + norm_path + ": " + e_node.what());
                                // Potentially skip this address or mark error
                            }
                        }
                        if (addresses_str.empty() && !node_ids.empty()){
                             Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Node IDs found for " + norm_path + " but could not retrieve any addresses.");
                             res_msg._ErrorCode = EHOSTUNREACH; // Cannot find addresses for listed nodes
                        } else {
                            res_msg._Data = addresses_str;
                            res_msg._ErrorCode = 0;
                             Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Sending node locations for " + norm_path + ": " + addresses_str);
                        }
                    }
                } catch (const std::runtime_error& e_file) { // Catch error from getFileNodes (file not found)
                    Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Error getting file nodes for " + norm_path + ": " + e_file.what());
                    res_msg._ErrorCode = ENOENT;
                }
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            case MessageType::PrepareWriteOperation:
            {
                Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Received PrepareWriteOperation for: " + request._Path + " Offset: " + std::to_string(request._Offset) + " Size: " + std::to_string(request._Size));
                Message res_msg;
                res_msg._Type = MessageType::PrepareWriteOperationResponse;
                std::string norm_path = normalize_path_to_filename(request._Path);
                uint64_t updated_size_dummy; // Not strictly needed for response, but writeFileData expects it

                try {
                    std::vector<std::string> node_ids = metadataManager.getFileNodes(norm_path);
                    if (node_ids.empty()) {
                        // This case might mean it's a new file.
                        // However, FUSE usually calls create then write. If create assigned nodes, getFileNodes should find them.
                        // If it's truly a write to a non-existent file without prior create, Metaserver needs robust addFile logic here or rely on prior create.
                        // For now, assume create has happened or getFileNodes would throw.
                        // If getFileNodes can return empty for a known file with no nodes (e.g. all nodes died), then this is a problem.
                        Logger::getInstance().log(LogLevel::WARN, "[Metaserver] PrepareWriteOperation: No nodes found for existing file: " + norm_path + ". This might indicate data loss or issues.");
                        res_msg._ErrorCode = ENOENT; // Or appropriate error
                    } else {
                        // Select primary node (e.g., first one)
                        // Ensure node ID is valid and get its address
                        std::string primary_node_address;
                        bool primary_node_found = false;
                        for(const auto& node_id : node_ids){ // Iterate to find a live one
                             try {
                                NodeInfo node_info = metadataManager.getNodeInfo(node_id); // Assuming getNodeInfo exists to check liveness
                                if(node_info.isAlive){
                                    primary_node_address = node_info.nodeAddress;
                                    primary_node_found = true;
                                    break;
                                }
                             } catch (const std::runtime_error& e_node_info){
                                 Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] PrepareWriteOperation: Error getting info for node ID " + node_id + ": " + e_node_info.what());
                             }
                        }

                        if (!primary_node_found) {
                             Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] PrepareWriteOperation: No live primary node found for file " + norm_path);
                             res_msg._ErrorCode = EHOSTUNREACH; // No live node to write to
                        } else {
                            res_msg._NodeAddress = primary_node_address;
                            // Update metadata size. Pass empty data string for data_to_write, as we are only preparing.
                            // The actual data length to be written is in request._Size.
                            // The offset is request._Offset.
                            // The writeFileData method will calculate the new total file size based on offset + data_to_write.length().
                            // We need to ensure it correctly calculates new total size if data_to_write is empty but a size is given.
                            // Let's assume writeFileData is smart or add a specific metadata update function.
                            // For now, we'll use it by passing a dummy string of request._Size to represent the new data segment.
                            // This is not ideal. A specific function like metadataManager.updateFileSize(path, offset, length) would be better.
                            // Let's assume current writeFileData updates based on offset + length of string.
                            // If request._Size is the *new* total size, that's different. FUSE write provides data to be written.
                            // The size of data to write is request._Size from the FUSE client.
                            // The metaserver's writeFileData takes the actual data to calculate new size.
                            // So, we pass a dummy string of that size or rely on metaserver to use request._Size from FUSE.
                            // The current Message struct doesn't pass the *data buffer* from FUSE to metaserver for PrepareWrite.
                            // It passes the *length* of the buffer in request._Size.
                            // MetadataManager::writeFileData needs to correctly interpret this.
                            // The current MM::writeFileData: out_size_written = data_to_write.length(); write_end_offset = offset + out_size_written;
                            // This is problematic if data_to_write is empty for PrepareWrite.
                            //
                            // TEMPORARY WORKAROUND: For PrepareWriteOperation, we'll let writeFileData only update if the new calculated size
                            // based on offset + request._Size (from FUSE, indicating data to be written) is larger.
                            // This means writeFileData needs to be aware of request._Size if data_to_write is empty.
                            // Or, more simply, Metaserver just provides the node, and FUSE adapter updates Metaserver with final size *after* node write.
                            // Let's go with: Metaserver provides primary node. Fuse writes to node. Fuse then tells Metaserver "I wrote X bytes at offset Y to file Z".
                            // So, PrepareWriteOperation doesn't need to call writeFileData itself. The FUSE client will send a separate UpdateSize/WriteFile msg to MS.
                            // For now, the problem states "It updates its internal file size metadata (metadataManager.writeFileData can still do this part)."
                            // This is tricky. Let's assume writeFileData is called but with empty data, and it has logic to use offset+size from request.
                            // This means changing writeFileData slightly or adding a new method.
                            // The current `writeFileData` uses `data_to_write.length()`. If `data_to_write` is empty (as it would be for `PrepareWriteOperation`), `out_size_written` becomes 0.
                            // Then `write_end_offset` is just `offset`. This would only update `fileSizes` if `offset` itself is greater than current size. This is NOT what we want for pre-allocation or size update based on intent.
                            //
                            // Let's defer complex size update in Metaserver during PrepareWrite. FUSE adapter will do the write to DataNode, then send WriteFile to Metaserver which WILL have the data.
                            // So, PrepareWriteOperation in MS only needs to provide the primary node. The existing WriteFile case will handle size update later.
                            // This simplifies Metaserver's PrepareWriteOperation.

                            Logger::getInstance().log(LogLevel::INFO, "[Metaserver] Sending primary node " + primary_node_address + " for write to " + norm_path);
                            res_msg._ErrorCode = 0;
                        }
                    }
                } catch (const std::runtime_error& e_file) { // Catch error from getFileNodes (file not found)
                    Logger::getInstance().log(LogLevel::ERROR, "[Metaserver] Error getting file nodes for " + norm_path + " in PrepareWrite: " + e_file.what());
                    res_msg._ErrorCode = ENOENT;
                }
                server.Send(Message::Serialize(res_msg).c_str(), _pClient);
                break;
            }
            default:
                Logger::getInstance().log(LogLevel::WARN, "Received unhandled or unknown message type: " + std::to_string(static_cast<int>(request._Type)) + " from client " + server.GetClientIPAddress(_pClient));
                Message err_res;
                err_res._Type = request._Type;
                err_res._ErrorCode = ENOSYS;
                server.Send(Message::Serialize(err_res).c_str(), _pClient);
                break;
        }

        if (shouldSave) {
        Logger::getInstance().log(LogLevel::INFO, "Saving metadata state.");
        metadataManager.saveMetadata("file_metadata.dat", "node_registry.dat");
    }
    } catch (const Networking::NetworkException& ne) {
        Logger::getInstance().log(LogLevel::ERROR, "Network error in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(ne.what()));
    } catch (const std::runtime_error& re) {
        Logger::getInstance().log(LogLevel::ERROR, "Runtime error (e.g., deserialization) in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(re.what()));
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "Generic exception in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(e.what()));
    }  catch (...) {
        Logger::getInstance().log(LogLevel::ERROR, "Unknown exception in HandleClientConnection for " + server.GetClientIPAddress(_pClient));
    }
}

// main() function has been moved to src/main_metaserver.cpp
