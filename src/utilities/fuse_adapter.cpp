#include "utilities/fuse_adapter.h"
#include "utilities/logger.h"
#include <sstream> // For std::ostringstream
#include <chrono>    // For timestamps
#include <iomanip>   // For std::put_time
#include "utilities/client.h"
#include "utilities/message.h"
#include <errno.h>
#include <string.h> // For memset, strerror, strcmp
#include <unistd.h> // for getuid, getgid
#include <time.h>   // for time()
#include <sys/stat.h> // For S_IFDIR, S_IFREG modes, and struct statx

#ifndef STATX_ATTR_DIRECTORY
#include <linux/stat.h>
#endif

#include <sys/xattr.h> // For XATTR_USER_PREFIX
#include <string>       // For std::string, std::stoi, std::to_string
#include <stdexcept>    // For std::invalid_argument, std::out_of_range, std::exception
#include <sstream>      // For std::istringstream
#include <algorithm>    // For std::min

// Helper for timestamp logging
static std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Helper to get SimpliDfsFuseData from FUSE context
static SimpliDfsFuseData* get_fuse_data() {
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(fuse_get_context()->private_data);
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] FUSE private_data not configured correctly or metadata_client not accessible or configured.");
        return nullptr;
    }
    return data;
}

int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Entry for path: " + path_str);
    memset(stbuf, 0, sizeof(struct stat));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    // Handle root directory locally
    if (path_str == "/") {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2; // Standard for directories (.) and (..)
        stbuf->st_uid = getuid(); // Current user
        stbuf->st_gid = getgid(); // Current group
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL); // Current time
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (handled root) for path: " + path_str);
        return 0;
    }

    // For other paths, query the metaserver
    Message req_msg;
    req_msg._Type = MessageType::GetAttr;
    req_msg._Path = path_str; // Send full path

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Before Send for path: " + path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: After Send for path: " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Failed to send GetAttr request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Before Receive for path: " + path_str);
        std::vector<char> received_vector_getattr = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: After Receive for path: " + path_str + ", Received size: " + std::to_string(received_vector_getattr.size()));
        if (received_vector_getattr.empty() && send_success) { // Check send_success to ensure this isn't a cascade from send failure
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Received empty response for GetAttr request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_getattr.begin(), received_vector_getattr.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Received GetAttr response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode; // Return negative errno
        }

        // Populate stbuf from res_msg
        stbuf->st_mode = static_cast<mode_t>(res_msg._Mode);
        stbuf->st_uid = static_cast<uid_t>(res_msg._Uid);
        stbuf->st_gid = static_cast<gid_t>(res_msg._Gid);
        stbuf->stx_size = static_cast<off_t>(res_msg._Size); // Corrected from stbuf->st_size
        stbuf->st_nlink = (S_ISDIR(stbuf->st_mode)) ? 2 : 1; // Basic nlink logic

        // Timestamps: Use current time as placeholder, ideally server provides these
        stbuf->st_atime = time(NULL);
        stbuf->st_mtime = time(NULL);
        stbuf->st_ctime = time(NULL);

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (success) for path: " + path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

#ifdef SIMPLIDFS_HAS_STATX
int simpli_statx(const char *path, struct statx *stxbuf, int flags_unused, struct fuse_file_info *fi) {
    (void)fi;
    (void)flags_unused;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Entry for path: " + path_str);
    memset(stxbuf, 0, sizeof(struct statx));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

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


    if (path_str == "/") {
        stxbuf->stx_mode = S_IFDIR | 0755;
        stxbuf->stx_nlink = 2;
        stxbuf->stx_size = 4096;
        stxbuf->stx_attributes_mask |= STATX_ATTR_DIRECTORY;
        stxbuf->stx_mask |= STATX_BASIC_STATS | STATX_BTIME; // Indicate what fields are filled
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Exit (handled root) for path: " + path_str);
        return 0;
    }

    std::string filename = path_str.substr(1);
    // TODO: Implement network call to metaserver for Statx (similar to GetAttr but with statx structure)
    // Add logging before/after Send/Receive here when implemented.

    Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Statx not yet fully implemented for: " + filename + ". Returning ENOENT.");
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Exit (not implemented) for path: " + path_str);
    return -ENOENT;
}
#endif // SIMPLIDFS_HAS_STATX

int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void)flags;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Entry for path: " + path_str);

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    filler(buf, ".", NULL, 0, (enum fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (enum fuse_fill_dir_flags)0);

    Message req_msg;
    req_msg._Type = MessageType::Readdir;
    req_msg._Path = path_str; // Send the full path

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Before Send for " + path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Failed to send Readdir request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Before Receive for " + path_str);
        std::vector<char> received_vector_readdir = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_readdir.size()));

        if (received_vector_readdir.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Received empty response for Readdir request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_readdir.begin(), received_vector_readdir.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Received Readdir response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
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
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (success) for path: " + path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_open(const char *path, struct fuse_file_info *fi) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Entry for path: " + path_str + " with flags: " + std::to_string(fi->flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Write access denied for directory /");
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (root write access denied) for path: " + path_str);
            return -EACCES;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (root read access) for path: " + path_str);
        return 0;
    }

    Message req_msg;
    req_msg._Type = MessageType::Open;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(fi->flags); // Using _Mode to pass FUSE flags

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Before Send for " + path_str + " with flags " + std::to_string(fi->flags));
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Failed to send Open request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Before Receive for " + path_str);
        std::vector<char> received_vector_open = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_open.size()));

        if (received_vector_open.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Received empty response for Open request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_open.begin(), received_vector_open.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Received Open response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode;
        }

        fi->fh = 1; // Dummy file handle
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (success) for path: " + path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi; // fi->fh could be used if server manages file handles
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    Message req_msg;
    req_msg._Type = MessageType::Read;
    req_msg._Path = path_str;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Before Send for " + path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Failed to send Read request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Before Receive for " + path_str);
        std::vector<char> received_vector_read = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_read.size()));

        if (received_vector_read.empty() && size > 0 && send_success) { // Check send_success
             Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Received completely empty (zero bytes) network response for " + path_str + " when requesting " + std::to_string(size) + " bytes. This might be EOF or an issue.");
        }
        std::string serialized_res(received_vector_read.begin(), received_vector_read.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Received Read response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode) + ", Data size: " + std::to_string(res_msg._Data.length()));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode;
        }

        size_t bytes_to_copy = std::min(size, static_cast<size_t>(res_msg._Data.length()));
        memcpy(buf, res_msg._Data.data(), bytes_to_copy);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exit (success, copied " + std::to_string(bytes_to_copy) + " bytes) for path: " + path_str);
        return static_cast<ssize_t>(bytes_to_copy);

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_access(const char *path, int mask) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Entry for path: " + path_str + " with mask: " + std::to_string(mask));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (root access granted) for path: " + path_str);
        return 0; // Root is generally accessible
    }

    Message req_msg;
    req_msg._Type = MessageType::Access;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(mask);

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Before Send for " + path_str + " with mask " + std::to_string(mask));
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Failed to send Access request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Before Receive for " + path_str);
        std::vector<char> received_vector_access = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_access.size()));
        if (received_vector_access.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Received empty response for Access request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_access.begin(), received_vector_access.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Received Access response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        int result = (res_msg._ErrorCode == 0) ? 0 : -res_msg._ErrorCode;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (result " + std::to_string(result) + ") for path: " + path_str);
        return result;

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    std::string path_str(path);
    std::ostringstream oss_create_log_entry;
    oss_create_log_entry << getCurrentTimestamp() << " [FUSE_ADAPTER] simpli_create: Entry for path: " << path_str << " with mode: " << std::oct << mode << std::dec << ", flags: " << fi->flags;
    Logger::getInstance().log(LogLevel::DEBUG, oss_create_log_entry.str());

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Cannot create file at root path /.");
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (cannot create root) for path: " + path_str);
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::CreateFile;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(mode);

    try {
        std::ostringstream oss_create_send_log;
        oss_create_send_log << getCurrentTimestamp() << " [FUSE_ADAPTER] simpli_create: Before Send for " << path_str << " with mode " << std::oct << mode;
        Logger::getInstance().log(LogLevel::DEBUG, oss_create_send_log.str());
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Failed to send CreateFile request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Before Receive for " + path_str);
        std::vector<char> received_vector_create = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_create.size()));
        if (received_vector_create.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Received empty response for CreateFile request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_create.begin(), received_vector_create.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Received CreateFile response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode;
        }

        fi->fh = 1;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (success) for path: " + path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    Message req_msg;
    req_msg._Type = MessageType::Write;
    req_msg._Path = path_str;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);
    req_msg._Data.assign(buf, size);

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Before Send for " + path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Failed to send Write request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Before Receive for " + path_str);
        std::vector<char> received_vector_write = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_write.size()));
        if (received_vector_write.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Received empty response for Write request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_write.begin(), received_vector_write.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Received Write response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode) + ", Size Confirmed: " + std::to_string(res_msg._Size));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode;
        }

        ssize_t bytes_written = static_cast<ssize_t>(res_msg._Size);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (success, wrote " + std::to_string(bytes_written) + " bytes) for path: " + path_str);
        return bytes_written;

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_unlink(const char *path) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Entry for path: " + path_str);

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (data is null) for path: " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Cannot unlink root directory.");
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (cannot unlink root) for path: " + path_str);
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::Unlink;
    req_msg._Path = path_str;

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Before Send for " + path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: After Send for " + path_str + ", Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Failed to send Unlink request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (send failed) for path: " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Before Receive for " + path_str);
        std::vector<char> received_vector_unlink = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: After Receive for " + path_str + ", Received size: " + std::to_string(received_vector_unlink.size()));
        if (received_vector_unlink.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Received empty response for Unlink request for " + path_str);
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (empty receive) for path: " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_unlink.begin(), received_vector_unlink.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Received Unlink response for " + path_str + ", ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for path: " + path_str);
            return -res_msg._ErrorCode;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (success) for path: " + path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exception for " + path_str + ": " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exit (exception) for path: " + path_str);
        return -EIO;
    }
}

int simpli_rename(const char *from_path, const char *to_path, unsigned int flags) {
    (void)flags; // Ignoring flags for now
    std::string from_path_str(from_path);
    std::string to_path_str(to_path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Entry from: " + from_path_str + " to: " + to_path_str + " with flags: " + std::to_string(flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (data is null) for " + from_path_str + " -> " + to_path_str);
        return -EIO;
    }

    if (from_path_str == "/" || to_path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Cannot rename to or from root directory.");
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (cannot rename root) for " + from_path_str + " -> " + to_path_str);
        return -EBUSY;
    }

    Message req_msg;
    req_msg._Type = MessageType::Rename;
    req_msg._Path = from_path_str;
    req_msg._NewPath = to_path_str;

    try {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Before Send from " + from_path_str + " to " + to_path_str);
        std::string serialized_req = Message::Serialize(req_msg);
        bool send_success = data->metadata_client->Send(serialized_req.c_str());
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: After Send, Success: " + (send_success ? "true" : "false"));
        if (!send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Failed to send Rename request.");
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (send failed) for " + from_path_str + " -> " + to_path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Before Receive for " + from_path_str + " -> " + to_path_str);
        std::vector<char> received_vector_rename = data->metadata_client->Receive();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: After Receive, Received size: " + std::to_string(received_vector_rename.size()));
        if (received_vector_rename.empty() && send_success) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Received empty response for Rename request.");
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (empty receive) for " + from_path_str + " -> " + to_path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector_rename.begin(), received_vector_rename.end());
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Received Rename response, ErrorCode: " + std::to_string(res_msg._ErrorCode));

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (error code " + std::to_string(-res_msg._ErrorCode) + ") for " + from_path_str + " -> " + to_path_str);
            return -res_msg._ErrorCode;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (success) for " + from_path_str + " -> " + to_path_str);
        return 0; // Success

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exception: " + std::string(e.what()));
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exit (exception) for " + from_path_str + " -> " + to_path_str);
        return -EIO;
    }
}

int simpli_release(const char *path, struct fuse_file_info *fi) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Entry for path: " + path_str + " with fi->fh: " + std::to_string(fi->fh) + ". No server action implemented yet.");
    // No network calls currently.
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Exit for path: " + path_str);
    return 0;
}

int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void)fi; // fi can be NULL.
    std::string path_str(path);
    Logger& logger = Logger::getInstance();
    logger.log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_utimens: Entry for path: " + path_str);

    // TODO: Implement network call to metaserver for Utimens
    // Add logging before/after Send/Receive here when implemented.
    logger.log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_utimens: Operation not yet fully implemented for " + path_str + ". Optimistically succeeding.");
    logger.log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_utimens: Exit (not implemented) for path: " + path_str);
    return 0;
}

// main function for the FUSE adapter
int main(int argc, char *argv[]) {
    try {
        Logger::init("fuse_adapter_main.log", LogLevel::DEBUG); // Initialize logger early
    } catch (const std::exception& e) {
        fprintf(stderr, "%s [FUSE_ADAPTER] FATAL: Failed to initialize logger: %s\n", getCurrentTimestamp().c_str(), e.what());
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Starting SimpliDFS FUSE adapter.");

    if (argc < 4) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Usage: " + std::string(argv[0]) + " <metaserver_host> <metaserver_port> <mountpoint> [FUSE options]");
        return 1;
    }

    SimpliDfsFuseData fuse_data;
    fuse_data.metaserver_host = argv[1];
    try {
        fuse_data.metaserver_port = std::stoi(argv[2]);
    } catch (const std::invalid_argument& ia) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Invalid metaserver port: " + std::string(argv[2]) + ". Must be an integer. " + ia.what());
        return 1;
    } catch (const std::out_of_range& oor) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Metaserver port out of range: " + std::string(argv[2]) + ". " + oor.what());
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Attempting to connect to metaserver at " + fuse_data.metaserver_host + ":" + std::to_string(fuse_data.metaserver_port));

    fuse_data.metadata_client = new Networking::Client();
    bool connect_socket_success = false;
    try {
        // CreateClientTCPSocket is called internally by Client constructor or connectWithRetry
        // For direct control and logging:
        if (!fuse_data.metadata_client->InitClientSocket()) { // WSAStartup on Windows
             Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to initialize client socket (WSAStartup).");
             delete fuse_data.metadata_client;
             fuse_data.metadata_client = nullptr;
             return 1;
        }
        if (!fuse_data.metadata_client->CreateClientTCPSocket(fuse_data.metaserver_host.c_str(), fuse_data.metaserver_port)) {
            Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to create TCP socket for metaserver.");
            delete fuse_data.metadata_client;
            fuse_data.metadata_client = nullptr;
            return 1;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] main: Before ConnectClientSocket.");
        connect_socket_success = fuse_data.metadata_client->ConnectClientSocket();
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] main: After ConnectClientSocket, Success: " + (connect_socket_success ? "true" : "false"));

        if (!connect_socket_success) {
            Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to connect to metaserver.");
            delete fuse_data.metadata_client;
            fuse_data.metadata_client = nullptr;
            return 1;
        }
        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Successfully connected to metaserver.");
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Exception during metaserver connection: " + std::string(e.what()));
        if (fuse_data.metadata_client && !connect_socket_success) { // Ensure cleanup if ConnectClientSocket failed after allocation
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

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Passing control to fuse_main.");

    int fuse_ret = fuse_main(fuse_argc, fuse_argv, &simpli_ops, &fuse_data);

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] fuse_main returned " + std::to_string(fuse_ret) + ". Cleaning up.");
    delete[] fuse_argv;
    // Note: simpli_destroy will be called by FUSE to clean up fuse_data.metadata_client

    return fuse_ret;
}

void simpli_destroy(void* private_data) {
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Entry.");
    if (!private_data) {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: private_data is null.");
        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Exit (private_data null).");
        return;
    }
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(private_data);
    if (data->metadata_client) {
        if (data->metadata_client->IsConnected()) {
            try {
                Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Before Disconnect.");
                data->metadata_client->Disconnect();
                Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Disconnected from metaserver.");
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Exception during disconnect from metaserver: " + std::string(e.what()));
            }
        }
        delete data->metadata_client;
        data->metadata_client = nullptr;
        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Metadata client deleted.");
    } else {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: metadata_client was already null.");
    }
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Exit.");
}
