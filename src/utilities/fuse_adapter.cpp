#include "utilities/fuse_adapter.h"
#include "utilities/logger.h"
#include "utilities/client.h"
#include "utilities/message.h"
#include <errno.h>
#include <string.h> // For memset, strerror, strcmp
#include <unistd.h> // for getuid, getgid
#include <time.h>   // for time()
#include <sys/stat.h> // For S_IFDIR, S_IFREG modes, and struct statx
#include <sys/xattr.h> // For XATTR_USER_PREFIX
#include <string>       // For std::string, std::stoi, std::to_string
#include <stdexcept>    // For std::invalid_argument, std::out_of_range, std::exception
#include <sstream>      // For std::istringstream
#include <algorithm>    // For std::min
#include <vector>       // Required for std::vector in SimpliDfsFuseHandler

// Forward declaration if SimpliDfsFuseHandler is defined later or in another file
class SimpliDfsFuseHandler;

// Helper to get SimpliDfsFuseData from FUSE context
// Also initialize SimpliDfsFuseHandler if not already done (e.g. in fi->fh)
// For global functions not having fi, we might need a global handler instance or pass fuse_data.
static SimpliDfsFuseData* get_fuse_data() {
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(fuse_get_context()->private_data);
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, "FUSE private_data not configured correctly or metadata_client not accessible or configured.");
        return nullptr;
    }
    // SimpliDfsFuseHandler might be better stored in fi->fh for open/read/write/release
    // For other ops, if a global handler is needed:
    // if (!data->fuse_handler) {
    //    data->fuse_handler = new SimpliDfsFuseHandler(*(data->metadata_client));
    // }
    return data;
}


// Definition of SimpliDfsFuseHandler (can be moved to a separate .h/.cpp if it grows)
class SimpliDfsFuseHandler {
public:
    Networking::Client& ms_client; // Reference to metaserver client from SimpliDfsFuseData
    Logger& logger;

    SimpliDfsFuseHandler(Networking::Client& metaserver_client)
        : ms_client(metaserver_client), logger(Logger::getInstance()) {}

    // Core logic extracted from simpli_read
    int process_read(const std::string& path, char *buf, size_t size, off_t offset) {
        logger.log(LogLevel::DEBUG, "[FuseHandler] process_read for path: " + path + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

        // Phase 1: Get node locations from Metaserver
        Message loc_req_msg;
        loc_req_msg._Type = MessageType::GetFileNodeLocations;
        loc_req_msg._Path = path;

        std::string node_addresses_str;
        try {
            logger.log(LogLevel::DEBUG, "[FuseHandler] Sending GetFileNodeLocations request for " + path);
            std::string serialized_loc_req = Message::Serialize(loc_req_msg);
            if (!ms_client.Send(serialized_loc_req.c_str())) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Failed to send GetFileNodeLocations request for " + path);
                return -EIO;
            }

            std::vector<char> received_vec_loc = ms_client.Receive();
            if (received_vec_loc.empty()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Received empty response for GetFileNodeLocations from Metaserver for " + path);
                return -EIO;
            }
            std::string serialized_loc_res(received_vec_loc.begin(), received_vec_loc.end());
            Message loc_res_msg = Message::Deserialize(serialized_loc_res);

            if (loc_res_msg._ErrorCode != 0) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Metaserver returned error " + std::to_string(loc_res_msg._ErrorCode) + " for GetFileNodeLocations for " + path);
                return -loc_res_msg._ErrorCode;
            }
            if (loc_res_msg._Data.empty()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Metaserver returned no node locations for " + path);
                return -ENOENT;
            }
            node_addresses_str = loc_res_msg._Data;
            logger.log(LogLevel::INFO, "[FuseHandler] Received node locations for " + path + ": " + node_addresses_str);

        } catch (const std::exception& e) {
            logger.log(LogLevel::ERROR, "[FuseHandler] Exception communicating with Metaserver for GetFileNodeLocations " + path + ": " + std::string(e.what()));
            return -EIO;
        }

        // Phase 2: Iterate through available nodes and attempt to read data
        std::istringstream iss_nodes(node_addresses_str);
        std::string current_node_address;
        bool read_successful = false;
        ssize_t bytes_read_from_node = -EIO;

        while (std::getline(iss_nodes, current_node_address, ',')) {
            if (current_node_address.empty()) continue;
            logger.log(LogLevel::DEBUG, "[FuseHandler] Attempting to read from node: " + current_node_address + " for path " + path);

            size_t colon_pos = current_node_address.find(':');
            if (colon_pos == std::string::npos) {
                logger.log(LogLevel::WARN, "[FuseHandler] Invalid node address format '" + current_node_address + "'. Skipping.");
                bytes_read_from_node = -EIO;
                continue;
            }
            std::string node_ip = current_node_address.substr(0, colon_pos);
            int node_port;
            try {
                node_port = std::stoi(current_node_address.substr(colon_pos + 1));
            } catch (const std::exception& e) {
                logger.log(LogLevel::WARN, "[FuseHandler] Invalid port in node address '" + current_node_address + "': " + e.what() + ". Skipping.");
                bytes_read_from_node = -EIO;
                continue;
            }

            Networking::Client data_node_client; // Create a new client for each attempt
            try {
                logger.log(LogLevel::INFO, "[FuseHandler] Connecting to data node " + node_ip + ":" + std::to_string(node_port) + " for file " + path);
                if (!data_node_client.CreateClientTCPSocket(node_ip.c_str(), node_port) || !data_node_client.ConnectClientSocket()) {
                     logger.log(LogLevel::WARN, "[FuseHandler] Failed to connect to data node " + current_node_address + ". Trying next node if available.");
                     bytes_read_from_node = -EHOSTUNREACH;
                     continue;
                }

                Message data_req_msg;
                data_req_msg._Type = MessageType::ReadFile;
                data_req_msg._Filename = path;
                data_req_msg._Offset = static_cast<int64_t>(offset);
                data_req_msg._Size = static_cast<uint64_t>(size);

                std::string serialized_data_req = Message::Serialize(data_req_msg);
                if (!data_node_client.Send(serialized_data_req.c_str())) {
                    logger.log(LogLevel::WARN, "[FuseHandler] Failed to send ReadFile request to data node " + current_node_address + ". Trying next node if available.");
                    data_node_client.Disconnect();
                    bytes_read_from_node = -EIO;
                    continue;
                }

                std::vector<char> received_vec_data = data_node_client.Receive();
                data_node_client.Disconnect();

                if (received_vec_data.empty() && size > 0) {
                     logger.log(LogLevel::WARN, "[FuseHandler] Received empty (zero bytes) network response from data node " + current_node_address + " for " + path);
                }

                std::string serialized_data_res(received_vec_data.begin(), received_vec_data.end());
                Message data_res_msg = Message::Deserialize(serialized_data_res);

                if (data_res_msg._Type == MessageType::ReadFileResponse && data_res_msg._ErrorCode == 0) {
                    size_t bytes_to_copy = std::min(size, static_cast<size_t>(data_res_msg._Data.length()));
                    memcpy(buf, data_res_msg._Data.data(), bytes_to_copy);
                    logger.log(LogLevel::INFO, "[FuseHandler] Successfully read " + std::to_string(bytes_to_copy) + " bytes from data node " + current_node_address + " for " + path);
                    bytes_read_from_node = static_cast<ssize_t>(bytes_to_copy);
                    read_successful = true;
                    break;
                } else {
                    logger.log(LogLevel::WARN, "[FuseHandler] Data node " + current_node_address + " returned error " + std::to_string(data_res_msg._ErrorCode) + " (Type: " + std::to_string(static_cast<int>(data_res_msg._Type)) + ") for " + path + ". Trying next node.");
                    bytes_read_from_node = - (data_res_msg._ErrorCode != 0 ? data_res_msg._ErrorCode : EIO) ;
                }
            } catch (const Networking::NetworkException& ne) {
                logger.log(LogLevel::WARN, "[FuseHandler] NetworkException with data node " + current_node_address + " for " + path + ": " + ne.what() + ". Trying next node.");
                if(data_node_client.IsConnected()) data_node_client.Disconnect();
                bytes_read_from_node = -EIO;
            } catch (const std::exception& e) {
                logger.log(LogLevel::WARN, "[FuseHandler] Exception with data node " + current_node_address + " for " + path + ": " + e.what() + ". Trying next node.");
                if(data_node_client.IsConnected()) data_node_client.Disconnect();
                bytes_read_from_node = -EIO;
            }
        }

        if (!read_successful) {
            logger.log(LogLevel::ERROR, "[FuseHandler] Failed to read from all available nodes for path " + path + ". Last error: " + std::to_string(bytes_read_from_node));
        }
        return bytes_read_from_node;
    }

    // Core logic extracted from simpli_write
    int process_write(const std::string& path, const char *buf, size_t size, off_t offset) {
        logger.log(LogLevel::DEBUG, "[FuseHandler] process_write for path: " + path + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

        // Phase 1: Prepare write operation with Metaserver (get primary data node)
        Message prep_req_msg;
        prep_req_msg._Type = MessageType::PrepareWriteOperation;
        prep_req_msg._Path = path;
        prep_req_msg._Offset = static_cast<int64_t>(offset);
        prep_req_msg._Size = static_cast<uint64_t>(size);

        std::string primary_node_address;
        try {
            logger.log(LogLevel::DEBUG, "[FuseHandler] Sending PrepareWriteOperation request for " + path);
            std::string serialized_prep_req = Message::Serialize(prep_req_msg);
            if (!ms_client.Send(serialized_prep_req.c_str())) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Failed to send PrepareWriteOperation request for " + path);
                return -EIO;
            }

            std::vector<char> received_vec_prep = ms_client.Receive();
            if (received_vec_prep.empty()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Received empty response for PrepareWriteOperation from Metaserver for " + path);
                return -EIO;
            }
            std::string serialized_prep_res(received_vec_prep.begin(), received_vec_prep.end());
            Message prep_res_msg = Message::Deserialize(serialized_prep_res);

            if (prep_res_msg._ErrorCode != 0) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Metaserver returned error " + std::to_string(prep_res_msg._ErrorCode) + " for PrepareWriteOperation for " + path);
                return -prep_res_msg._ErrorCode;
            }
            if (prep_res_msg._NodeAddress.empty()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Metaserver returned no primary node address for " + path);
                return -EHOSTUNREACH;
            }
            primary_node_address = prep_res_msg._NodeAddress;
            logger.log(LogLevel::INFO, "[FuseHandler] Received primary data node " + primary_node_address + " for " + path);

        } catch (const std::exception& e) {
            logger.log(LogLevel::ERROR, "[FuseHandler] Exception communicating with Metaserver for PrepareWriteOperation " + path + ": " + std::string(e.what()));
            return -EIO;
        }

        // Phase 2: Write data to the primary data node
        size_t colon_pos = primary_node_address.find(':');
        if (colon_pos == std::string::npos) {
            logger.log(LogLevel::ERROR, "[FuseHandler] Invalid primary node address format: " + primary_node_address);
            return -EIO;
        }
        std::string node_ip = primary_node_address.substr(0, colon_pos);
        int node_port;
        try {
            node_port = std::stoi(primary_node_address.substr(colon_pos + 1));
        } catch (const std::exception& e) {
            logger.log(LogLevel::ERROR, "[FuseHandler] Invalid port in primary node address " + primary_node_address + ": " + e.what());
            return -EIO;
        }

        Networking::Client data_node_client;
        ssize_t bytes_written_on_node = 0;
        try {
            logger.log(LogLevel::INFO, "[FuseHandler] Connecting to primary data node " + node_ip + ":" + std::to_string(node_port) + " for file " + path);
            if (!data_node_client.CreateClientTCPSocket(node_ip.c_str(), node_port) || !data_node_client.ConnectClientSocket()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Failed to connect to primary data node " + primary_node_address);
                return -EHOSTUNREACH;
            }

            Message data_write_msg;
            data_write_msg._Type = MessageType::WriteFile;
            data_write_msg._Filename = path;
            data_write_msg._Offset = static_cast<int64_t>(offset);
            data_write_msg._Size = static_cast<uint64_t>(size);
            data_write_msg._Content.assign(buf, size);
            // _Data field is also available, _Content is used by current Node::processWriteFileRequest
            data_write_msg._Data.assign(buf, size);


            std::string serialized_data_write = Message::Serialize(data_write_msg);
            if (!data_node_client.Send(serialized_data_write.c_str())) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Failed to send WriteFile request to data node " + primary_node_address);
                data_node_client.Disconnect();
                return -EIO;
            }

            std::vector<char> received_vec_confirm = data_node_client.Receive();
            data_node_client.Disconnect();

            if (received_vec_confirm.empty()) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Received empty confirmation from data node " + primary_node_address + " for " + path);
                return -EIO;
            }

            std::string serialized_confirm_res(received_vec_confirm.begin(), received_vec_confirm.end());
            Message confirm_res_msg = Message::Deserialize(serialized_confirm_res);

            if (confirm_res_msg._ErrorCode != 0) {
                logger.log(LogLevel::ERROR, "[FuseHandler] Data node " + primary_node_address + " returned error " + std::to_string(confirm_res_msg._ErrorCode) + " on write for " + path);
                return -confirm_res_msg._ErrorCode;
            }

            bytes_written_on_node = static_cast<ssize_t>(confirm_res_msg._Size);
            logger.log(LogLevel::INFO, "[FuseHandler] Successfully wrote " + std::to_string(bytes_written_on_node) + " bytes to data node " + primary_node_address + " for " + path);

            if (bytes_written_on_node >= 0) {
                Message update_meta_req;
                update_meta_req._Type = MessageType::UpdateFileMetadata;
                update_meta_req._Path = path;
                update_meta_req._Offset = static_cast<int64_t>(offset);
                update_meta_req._Size = static_cast<uint64_t>(bytes_written_on_node);

                logger.log(LogLevel::DEBUG, "[FuseHandler] Sending UpdateFileMetadata to Metaserver for " + path);
                try {
                    std::string serialized_update_req = Message::Serialize(update_meta_req);
                    if (!ms_client.Send(serialized_update_req.c_str())) {
                        logger.log(LogLevel::ERROR, "[FuseHandler] Failed to send UpdateFileMetadata request to Metaserver for " + path);
                    } else {
                        std::vector<char> received_vec_update_meta = ms_client.Receive();
                        if (received_vec_update_meta.empty()) {
                            logger.log(LogLevel::WARN, "[FuseHandler] Received empty response for UpdateFileMetadata from Metaserver for " + path);
                        } else {
                            Message update_meta_res = Message::Deserialize(std::string(received_vec_update_meta.begin(), received_vec_update_meta.end()));
                            if (update_meta_res._ErrorCode != 0) {
                                logger.log(LogLevel::WARN, "[FuseHandler] Metaserver failed to update metadata for " + path + ". Error: " + std::to_string(update_meta_res._ErrorCode));
                            } else {
                                logger.log(LogLevel::INFO, "[FuseHandler] Metaserver successfully updated metadata for " + path);
                            }
                        }
                    }
                } catch (const std::exception& e_meta_update) {
                    logger.log(LogLevel::WARN, "[FuseHandler] Exception sending UpdateFileMetadata to Metaserver for " + path + ": " + std::string(e_meta_update.what()));
                }
            }
            return bytes_written_on_node;

        } catch (const Networking::NetworkException& ne) {
            logger.log(LogLevel::ERROR, "[FuseHandler] NetworkException with data node " + primary_node_address + " for " + path + ": " + ne.what());
            if(data_node_client.IsConnected()) data_node_client.Disconnect();
            return -EIO;
        } catch (const std::exception& e) {
            logger.log(LogLevel::ERROR, "[FuseHandler] General std::exception for " + path + ": " + std::string(e.what()));
            if(data_node_client.IsConnected()) data_node_client.Disconnect();
            return -EIO;
        }
    }
};


int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr called for path: " + std::string(path));
    memset(stbuf, 0, sizeof(struct stat));

    SimpliDfsFuseData* data = get_fuse_data();
    // Note: get_fuse_data() already logs if data or metadata_client is null,
    // so we can directly return -EIO if it returns nullptr.
    if (!data) {
        // Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Critical error: FUSE data or metadata client is not available.");
        return -EIO;
    }

    // Handle root directory locally
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2; // Standard for directories (.) and (..)
        stbuf->st_uid = getuid(); // Current user
        stbuf->st_gid = getgid(); // Current group
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL); // Current time
        return 0;
    }

    // For other paths, query the metaserver
    Message req_msg;
    req_msg._Type = MessageType::GetAttr;
    req_msg._Path = path; // Send full path

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr: Sending GetAttr request for " + std::string(path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Failed to send GetAttr request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_getattr = data->metadata_client->Receive();
        if (received_vector_getattr.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Received empty response for GetAttr request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_getattr.begin(), received_vector_getattr.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr: Received GetAttr response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode; // Return negative errno
        }

        // Populate stbuf from res_msg
        stbuf->st_mode = static_cast<mode_t>(res_msg._Mode);
        stbuf->st_uid = static_cast<uid_t>(res_msg._Uid);
        stbuf->st_gid = static_cast<gid_t>(res_msg._Gid);
        stbuf->st_size = static_cast<off_t>(res_msg._Size);
        stbuf->st_nlink = (S_ISDIR(stbuf->st_mode)) ? 2 : 1; // Basic nlink logic

        // Timestamps: Use current time as placeholder, ideally server provides these
        stbuf->st_atime = time(NULL);
        stbuf->st_mtime = time(NULL);
        stbuf->st_ctime = time(NULL);

        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

#ifdef SIMPLIDFS_HAS_STATX
int simpli_statx(const char *path, struct statx *stxbuf, int flags_unused, struct fuse_file_info *fi) {
    (void)fi;
    (void)flags_unused;

    Logger::getInstance().log(LogLevel::DEBUG, "simpli_statx called for path: " + std::string(path));
    memset(stxbuf, 0, sizeof(struct statx));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO;

    std::string spath(path);

    stxbuf->stx_uid = getuid();
    stxbuf->stx_gid = getgid();
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
    stxbuf->stx_atime.tv_sec = current_time.tv_sec;
    stxbuf->stx_atime.tv_nsec = current_time.tv_nsec;
    stxbuf->stx_mtime.tv_sec = current_time.tv_sec;
    stxbuf->stx_mtime.tv_nsec = current_time.tv_nsec;
    stxbuf->stx_ctime.tv_sec = current_time.tv_sec;
    stxbuf->stx_ctime.tv_nsec = current_time.tv_nsec;
    stxbuf->stx_btime.tv_sec = current_time.tv_sec;
    stxbuf->stx_btime.tv_nsec = current_time.tv_nsec;


    if (spath == "/") {
        stxbuf->stx_mode = S_IFDIR | 0755;
        stxbuf->stx_nlink = 2;
        stxbuf->stx_size = 4096;
        stxbuf->stx_attributes_mask |= STATX_ATTR_DIRECTORY;
        stxbuf->stx_mask |= STATX_BASIC_STATS | STATX_BTIME; // Indicate what fields are filled
        return 0;
    }

    std::string filename = spath.substr(1);
    // Logger::getInstance().log(LogLevel::DEBUG, "simpli_statx: Evaluating filename: " + filename);
    // TODO: Implement network call to metaserver for Statx (similar to GetAttr but with statx structure)
    // if successful:
    //    stxbuf->stx_mode = ...
    //    stxbuf->stx_nlink = ...
    //    stxbuf->stx_size = ...
    //    stxbuf->stx_uid = ...
    //    stxbuf->stx_gid = ...
    //    stxbuf->stx_mask = STATX_BASIC_STATS | STATX_BTIME; // and other relevant STATX_ flags
    //    if (xattrs_present) stxbuf->stx_attributes_mask |= STATX_ATTR_HAS_XATTRS;
    //    return 0;

    Logger::getInstance().log(LogLevel::WARN, "simpli_statx: Statx not yet fully implemented for: " + filename + ". Returning ENOENT.");
    return -ENOENT;
}
#endif // SIMPLIDFS_HAS_STATX

int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void)flags;
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_readdir called for path: " + std::string(path));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    filler(buf, ".", NULL, 0, (enum fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (enum fuse_fill_dir_flags)0);

    Message req_msg;
    req_msg._Type = MessageType::Readdir;
    req_msg._Path = path; // Send the full path

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_readdir: Sending Readdir request for " + std::string(path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_readdir: Failed to send Readdir request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_readdir = data->metadata_client->Receive();
        if (received_vector_readdir.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_readdir: Received empty response for Readdir request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_readdir.begin(), received_vector_readdir.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_readdir: Received Readdir response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        // Parse res_msg._Data (entries separated by null character '\0')
        std::istringstream name_stream(res_msg._Data);
        std::string name_token;
        while(std::getline(name_stream, name_token, '\0')) {
            if (!name_token.empty()) { // Ensure not to fill empty tokens if any
                filler(buf, name_token.c_str(), NULL, 0, (enum fuse_fill_dir_flags)0);
            }
        }
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_readdir: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_open(const char *path, struct fuse_file_info *fi) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_open called for path: " + std::string(path) + " with flags: " + std::to_string(fi->flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    if (strcmp(path, "/") == 0) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            Logger::getInstance().log(LogLevel::WARN, "simpli_open: Write access denied for directory /");
            return -EACCES;
        }
        return 0;
    }

    Message req_msg;
    req_msg._Type = MessageType::Open;
    req_msg._Path = path;
    req_msg._Mode = static_cast<uint32_t>(fi->flags); // Using _Mode to pass FUSE flags

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_open: Sending Open request for " + std::string(path) + " with flags " + std::to_string(fi->flags));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_open: Failed to send Open request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_open = data->metadata_client->Receive();
        if (received_vector_open.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_open: Received empty response for Open request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_open.begin(), received_vector_open.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_open: Received Open response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        fi->fh = 1; // Dummy file handle, as server doesn't manage them yet.
                    // Could use res_msg._FileHandle if server sent one.
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_open: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_read called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));
    SimpliDfsFuseData* fuse_data_ptr = get_fuse_data();
    if (!fuse_data_ptr) return -EIO;

    // It's better to store the handler in SimpliDfsFuseData if it's needed across multiple FUSE ops
    // or if its construction is expensive. For now, stack allocating if only used here.
    // If SimpliDfsFuseData had SimpliDfsFuseHandler* fuse_handler;
    // SimpliDfsFuseHandler* handler = fuse_data_ptr->fuse_handler;
    // if (!handler) { Logger::getInstance().log(LogLevel::ERROR, "FUSE handler not initialized in fuse_data."); return -EIO;}
    // For now, create on stack if SimpliDfsFuseData only holds the client:
    SimpliDfsFuseHandler handler(*(fuse_data_ptr->metadata_client));
    return handler.process_read(path, buf, size, offset);
}

int simpli_access(const char *path, int mask) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_access called for path: " + std::string(path) + " with mask: " + std::to_string(mask));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    if (strcmp(path, "/") == 0) {
        return 0; // Root is generally accessible
    }

    Message req_msg;
    req_msg._Type = MessageType::Access;
    req_msg._Path = path; // Send full path
    req_msg._Mode = static_cast<uint32_t>(mask); // Send FUSE access mask in _Mode

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_access: Sending Access request for " + std::string(path) + " with mask " + std::to_string(mask));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_access: Failed to send Access request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_access = data->metadata_client->Receive();
        if (received_vector_access.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_access: Received empty response for Access request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_access.begin(), received_vector_access.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_access: Received Access response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        // If ErrorCode is 0, access is granted (return 0).
        // Otherwise, ErrorCode contains the errno to be returned (e.g., ENOENT, EACCES),
        // so return -ErrorCode.
        return (res_msg._ErrorCode == 0) ? 0 : -res_msg._ErrorCode;

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_access: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    std::ostringstream oss_create_log;
    oss_create_log << "simpli_create called for path: " << path << " with mode: " << std::oct << mode << std::dec << ", flags: " << fi->flags;
    Logger::getInstance().log(LogLevel::DEBUG, oss_create_log.str());

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) { // data->metadata_client is checked by get_fuse_data
        return -EIO;
    }

    if (strcmp(path, "/") == 0) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Cannot create file at root path /.");
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::CreateFile;
    req_msg._Path = path;
    req_msg._Mode = static_cast<uint32_t>(mode);

    try {
        std::ostringstream oss_create_send_log;
        oss_create_send_log << "simpli_create: Sending CreateFile request for " << path << " with mode " << std::oct << mode;
        Logger::getInstance().log(LogLevel::DEBUG, oss_create_send_log.str());
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Failed to send CreateFile request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_create = data->metadata_client->Receive();
        if (received_vector_create.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Received empty response for CreateFile request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_create.begin(), received_vector_create.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Received CreateFile response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        fi->fh = 1; // Dummy file handle, as server doesn't manage them yet.
                    // Could use res_msg._FileHandle if server sent one.
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_write called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));
    SimpliDfsFuseData* fuse_data_ptr = get_fuse_data();
    if (!fuse_data_ptr) return -EIO;

    SimpliDfsFuseHandler handler(*(fuse_data_ptr->metadata_client));
    return handler.process_write(path, buf, size, offset);
}

int simpli_unlink(const char *path) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_unlink called for path: " + std::string(path));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    if (strcmp(path, "/") == 0) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Cannot unlink root directory.");
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::Unlink;
    req_msg._Path = path;

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_unlink: Sending Unlink request for " + std::string(path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Failed to send Unlink request for " + std::string(path));
            return -EIO;
        }

        std::vector<char> received_vector_unlink = data->metadata_client->Receive();
        if (received_vector_unlink.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Received empty response for Unlink request for " + std::string(path));
            return -EIO;
        }
        std::string serialized_res(received_vector_unlink.begin(), received_vector_unlink.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_unlink: Received Unlink response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_rename(const char *from_path, const char *to_path, unsigned int flags) {
    (void)flags; // Ignoring flags for now, as per instruction
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_rename called from: " + std::string(from_path) + " to: " + std::string(to_path) + " with flags: " + std::to_string(flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    if (strcmp(from_path, "/") == 0 || strcmp(to_path, "/") == 0) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Cannot rename to or from root directory.");
        return -EBUSY;
    }

    Message req_msg;
    req_msg._Type = MessageType::Rename;
    req_msg._Path = from_path;
    req_msg._NewPath = to_path;

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_rename: Sending Rename request from " + std::string(from_path) + " to " + std::string(to_path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Failed to send Rename request.");
            return -EIO;
        }

        std::vector<char> received_vector_rename = data->metadata_client->Receive();
        if (received_vector_rename.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Received empty response for Rename request.");
            return -EIO;
        }
        std::string serialized_res(received_vector_rename.begin(), received_vector_rename.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_rename: Received Rename response, ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Exception: " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_release(const char *path, struct fuse_file_info *fi) {
    (void)path; // Path is not used in this stub
    (void)fi;   // File info (including fh) is not used in this stub
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_release called for path: " + std::string(path) + " with fi->fh: " + std::to_string(fi->fh) + ". No server action implemented yet.");
    // Since the server isn't managing file handles (fi->fh is a dummy value),
    // there's no specific "close" message to send to the server at this time.
    // If the server were to manage file handles, a MessageType::Close would be sent here.
    return 0;
}

int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void)fi; // fi can be NULL.
    Logger& logger = Logger::getInstance();
    logger.log(LogLevel::DEBUG, "simpli_utimens called for path: " + std::string(path));

    // TODO: Implement network call to metaserver for Utimens
    // Serialize timespec tv into msg._Data or specific fields if added to Message struct
    // Message msg_req;
    // msg_req._Type = MessageType::Utimens;
    // msg_req._Path = std::string(path).substr(1); // Assuming path starts with /
    // // Logic to serialize tv[0] (atime) and tv[1] (mtime) into msg_req._Data
    // // e.g., "sec1:nsec1|sec2:nsec2" or binary representation
    // SimpliDfsFuseData* data = get_fuse_data();
    // if (!data || !data->metadata_client) return -EIO;
    // data->metadata_client->Send(Message::Serialize(msg_req));
    // std::string response_str = data->metadata_client->Receive();
    // Message msg_res = Message::Deserialize(response_str);
    // return -msg_res._ErrorCode;

    logger.log(LogLevel::INFO, "simpli_utimens: Operation not yet fully implemented for " + std::string(path) + ". Optimistically succeeding.");
    return 0;
}

// main function for the FUSE adapter
int main(int argc, char *argv[]) {
    try {
        Logger::init("fuse_adapter_main.log", LogLevel::DEBUG);
    } catch (const std::exception& e) {
        fprintf(stderr, "FATAL: Failed to initialize logger: %s\n", e.what());
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Starting SimpliDFS FUSE adapter.");

    if (argc < 4) {
        Logger::getInstance().log(LogLevel::FATAL, "Usage: " + std::string(argv[0]) + " <metaserver_host> <metaserver_port> <mountpoint> [FUSE options]");
        return 1;
    }

    SimpliDfsFuseData fuse_data;
    fuse_data.metaserver_host = argv[1];
    try {
        fuse_data.metaserver_port = std::stoi(argv[2]);
    } catch (const std::invalid_argument& ia) {
        Logger::getInstance().log(LogLevel::FATAL, "Invalid metaserver port: " + std::string(argv[2]) + ". Must be an integer. " + ia.what());
        return 1;
    } catch (const std::out_of_range& oor) {
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver port out of range: " + std::string(argv[2]) + ". " + oor.what());
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Attempting to connect to metaserver at " + fuse_data.metaserver_host + ":" + std::to_string(fuse_data.metaserver_port));

    fuse_data.metadata_client = new Networking::Client();
    try {
        if (!fuse_data.metadata_client->CreateClientTCPSocket(fuse_data.metaserver_host.c_str(), fuse_data.metaserver_port)) {
            Logger::getInstance().log(LogLevel::FATAL, "Failed to create TCP socket for metaserver.");
            delete fuse_data.metadata_client;
            fuse_data.metadata_client = nullptr;
            return 1;
        }
        if (!fuse_data.metadata_client->ConnectClientSocket()) {
            Logger::getInstance().log(LogLevel::FATAL, "Failed to connect to metaserver.");
            delete fuse_data.metadata_client;
            fuse_data.metadata_client = nullptr;
            return 1;
        }
        Logger::getInstance().log(LogLevel::INFO, "Successfully connected to metaserver.");
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::FATAL, "Exception during metaserver connection: " + std::string(e.what()));
        if (fuse_data.metadata_client) {
            delete fuse_data.metadata_client;
            fuse_data.metadata_client = nullptr;
        }
        return 1;
    }

    char** fuse_argv = new char*[argc - 2];
    fuse_argv[0] = argv[0];
    for (int i = 3; i < argc; ++i) {
        fuse_argv[i - 2] = argv[i];
    }
    int fuse_argc = argc - 2;

    struct fuse_operations simpli_ops;
    memset(&simpli_ops, 0, sizeof(struct fuse_operations));

    simpli_ops.getattr = simpli_getattr;
    simpli_ops.readdir = simpli_readdir;
    simpli_ops.open    = simpli_open;
    simpli_ops.read    = simpli_read;
    simpli_ops.access  = simpli_access;
    simpli_ops.create  = simpli_create;
    simpli_ops.write   = simpli_write;
    simpli_ops.unlink  = simpli_unlink;
    simpli_ops.rename  = simpli_rename;
    simpli_ops.release = simpli_release;
    simpli_ops.utimens = simpli_utimens;
    simpli_ops.destroy = simpli_destroy;
#ifdef SIMPLIDFS_HAS_STATX
    simpli_ops.statx   = simpli_statx;
#endif

    Logger::getInstance().log(LogLevel::INFO, "Passing control to fuse_main.");

    int fuse_ret = fuse_main(fuse_argc, fuse_argv, &simpli_ops, &fuse_data);

    delete[] fuse_argv;

    return fuse_ret;
}

void simpli_destroy(void* private_data) {
    Logger::getInstance().log(LogLevel::INFO, "simpli_destroy called.");
    if (!private_data) {
        return;
    }
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(private_data);
    if (data->metadata_client) {
        if (data->metadata_client->IsConnected()) {
            try {
                data->metadata_client->Disconnect();
                Logger::getInstance().log(LogLevel::INFO, "Disconnected from metaserver.");
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, "Exception during disconnect from metaserver: " + std::string(e.what()));
            }
        }
        delete data->metadata_client;
        data->metadata_client = nullptr;
        Logger::getInstance().log(LogLevel::INFO, "Metadata client deleted.");
    }
}
