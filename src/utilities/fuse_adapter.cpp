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

// Helper to get SimpliDfsFuseData from FUSE context
static SimpliDfsFuseData* get_fuse_data() {
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(fuse_get_context()->private_data);
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, "FUSE private_data not configured correctly or metadata_client not accessible or configured.");
        return nullptr;
    }
    return data;
}

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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Failed to send GetAttr request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_getattr: Received empty response for GetAttr request for " + std::string(path));
            return -EIO;
        }
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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_readdir: Failed to send Readdir request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_readdir: Received empty response for Readdir request for " + std::string(path));
            return -EIO;
        }
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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_open: Failed to send Open request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_open: Received empty response for Open request for " + std::string(path));
            return -EIO;
        }
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
    (void)fi; // fi->fh could be used if server manages file handles
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_read called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    Message req_msg;
    req_msg._Type = MessageType::Read;
    req_msg._Path = path;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_read: Sending Read request for " + std::string(path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_read: Failed to send Read request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty() && size > 0) { // Empty response is only OK if 0 bytes requested or EOF
             Logger::getInstance().log(LogLevel::ERROR, "simpli_read: Received empty response for Read request for " + std::string(path));
             return -EIO;
        }
        Message res_msg = Message::Deserialize(serialized_res);
         Logger::getInstance().log(LogLevel::DEBUG, "simpli_read: Received Read response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode) + ", Data size: " + std::to_string(res_msg._Data.length()));


        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        size_t bytes_to_copy = std::min(size, static_cast<size_t>(res_msg._Data.length()));
        memcpy(buf, res_msg._Data.data(), bytes_to_copy);

        return static_cast<ssize_t>(bytes_to_copy); // FUSE read returns ssize_t

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_read: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_access: Failed to send Access request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_access: Received empty response for Access request for " + std::string(path));
            return -EIO;
        }
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
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_create called for path: " + std::string(path) + " with mode: " + std::oct + mode + std::dec + ", flags: " + std::to_string(fi->flags));

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
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Sending CreateFile request for " + std::string(path) + " with mode " + std::oct + mode);
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Failed to send CreateFile request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Received empty response for CreateFile request for " + std::string(path));
            return -EIO;
        }
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
    (void)fi; // fi->fh could be used if server manages file handles
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_write called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        return -EIO;
    }

    Message req_msg;
    req_msg._Type = MessageType::Write;
    req_msg._Path = path;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);
    req_msg._Data.assign(buf, size);

    try {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_write: Sending Write request for " + std::string(path));
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_write: Failed to send Write request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_write: Received empty response for Write request for " + std::string(path));
            return -EIO;
        }
        Message res_msg = Message::Deserialize(serialized_res);
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_write: Received Write response for " + std::string(path) + ", ErrorCode: " + std::to_string(res_msg._ErrorCode) + ", Size Confirmed: " + std::to_string(res_msg._Size));

        if (res_msg._ErrorCode != 0) {
            return -res_msg._ErrorCode;
        }

        // Server confirms the number of bytes written in res_msg._Size
        return static_cast<ssize_t>(res_msg._Size);

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_write: Exception for " + std::string(path) + ": " + std::string(e.what()));
        return -EIO;
    }
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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Failed to send Unlink request for " + std::string(path));
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Received empty response for Unlink request for " + std::string(path));
            return -EIO;
        }
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
        if (!data->metadata_client->Send(serialized_req)) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Failed to send Rename request.");
            return -EIO;
        }

        std::string serialized_res = data->metadata_client->Receive();
        if (serialized_res.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Received empty response for Rename request.");
            return -EIO;
        }
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
