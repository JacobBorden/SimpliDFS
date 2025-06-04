#include "utilities/fuse_adapter.h" // Should be first for precompiled headers if used
#include "utilities/logger.h"
#include "utilities/client.h" // For Networking::Client
#include "utilities/message.h" // For Message, MessageType
// Standard C++ headers
#include <iostream> // For std::cerr, std::endl (better than fprintf to stderr for C++)
#include <string>
#include <vector>
#include <stdexcept> // For std::stoi, std::invalid_argument, std::out_of_range
#include <algorithm> // For std::min
#include <sstream>   // For std::ostringstream, std::istringstream
#include <iomanip>   // For std::put_time
#include <chrono>    // For std::chrono::*
// Standard C headers
#include <cstring>   // For memset, strerror, strcmp
#include <cerrno>    // For errno constants like EIO, ENOENT
#include <unistd.h>  // For getuid, getgid
#include <sys/stat.h> // For S_IFDIR, S_IFREG, struct stat
#include <sys/xattr.h> // For XATTR_USER_PREFIX (Linux-specific)
#include <ctime>     // For time_t, time()

// Conditional include for statx (Linux-specific)
// Ensure statx definitions are available if SIMPLIDFS_HAS_STATX is defined
#ifdef SIMPLIDFS_HAS_STATX
#ifndef STATX_ATTR_DIRECTORY // Guard against redefinition if already included by sys/stat.h
#include <linux/stat.h>
#endif
#endif

// FUSE include
#include <fuse.h>


// Helper for timestamp logging (ensure it's defined or accessible)
static std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}


// Forward declarations for FUSE operations
static SimpliDfsFuseData* get_fuse_data(); // Should be defined in this file or fuse_adapter.h
int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
#ifdef SIMPLIDFS_HAS_STATX
int simpli_statx(const char *path, struct statx *stxbuf, int flags_unused, struct fuse_file_info *fi);
#endif
int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int simpli_open(const char *path, struct fuse_file_info *fi);
int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int simpli_access(const char *path, int mask);
int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int simpli_unlink(const char *path);
int simpli_rename(const char *from_path, const char *to_path, unsigned int flags);
int simpli_release(const char *path, struct fuse_file_info *fi);
int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);
void simpli_destroy(void* private_data);


// main function for the FUSE adapter
int main(int argc, char *argv[]) {
    try {
        Logger::init("/tmp/fuse_adapter_main.log", LogLevel::DEBUG);
    } catch (const std::exception& e) {
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] FATAL: Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Starting SimpliDFS FUSE adapter.");
    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: main() started." << std::endl;

    if (argc < 4) {
        std::string usage = "Usage: " + std::string(argv[0]) + " <metaserver_host> <metaserver_port> <mountpoint> [FUSE options]";
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] " + usage);
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Not enough arguments. Expected at least 4, got " << argc << "." << std::endl;
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: " << usage << std::endl;
        return 1;
    }

    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Original arguments: argc=" << argc << std::endl;
    for(int i=0; i<argc; ++i) {
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Original argv[" << i << "] = " << (argv[i] ? argv[i] : "null") << std::endl;
    }

    SimpliDfsFuseData fuse_data;
    fuse_data.metaserver_host = argv[1];
    try {
        fuse_data.metaserver_port = std::stoi(argv[2]);
    } catch (const std::invalid_argument& ia) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Invalid metaserver port: " + std::string(argv[2]) + ". Must be an integer. Details: " + ia.what());
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Invalid metaserver port: " << argv[2] << ". " << ia.what() << std::endl;
        return 1;
    } catch (const std::out_of_range& oor) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Metaserver port out of range: " + std::string(argv[2]) + ". Details: " + oor.what());
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Metaserver port out of range: " << argv[2] << ". " << oor.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Attempting to connect to metaserver at " + fuse_data.metaserver_host + ":" + std::to_string(fuse_data.metaserver_port));

    fuse_data.metadata_client = new Networking::Client();
    try {
        if (!fuse_data.metadata_client->InitClientSocket()) {
             Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to initialize client socket subsystem.");
             delete fuse_data.metadata_client; fuse_data.metadata_client = nullptr;
             return 1;
        }
        if (!fuse_data.metadata_client->CreateClientTCPSocket(fuse_data.metaserver_host.c_str(), fuse_data.metaserver_port)) {
            Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to create TCP socket for metaserver connection.");
            delete fuse_data.metadata_client; fuse_data.metadata_client = nullptr;
            return 1;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] main: Before ConnectClientSocket.");
        if (!fuse_data.metadata_client->ConnectClientSocket()) {
            Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Failed to connect to metaserver at " + fuse_data.metaserver_host + ":" + std::to_string(fuse_data.metaserver_port));
            delete fuse_data.metadata_client; fuse_data.metadata_client = nullptr;
            return 1;
        }
        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] Successfully connected to metaserver.");
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Successfully connected to metaserver." << std::endl;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::FATAL, getCurrentTimestamp() + " [FUSE_ADAPTER] Exception during metaserver connection setup: " + std::string(e.what()));
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Exception during metaserver connection: " << e.what() << std::endl;
        if (fuse_data.metadata_client) {
            delete fuse_data.metadata_client; fuse_data.metadata_client = nullptr;
        }
        return 1;
    }

    // --- Argument rearrangement for fuse_main ---
    int new_argc = argc - 2;
    char** new_argv = new char*[new_argc];

    new_argv[0] = argv[0];    // Program name
    new_argv[1] = argv[3];    // Mount point (from original argv[3])

    for (int i = 4; i < argc; ++i) { // Copy FUSE options (originally from argv[4] onwards)
        new_argv[i - 2] = argv[i]; // to new_argv[2] onwards
    }
    // --- End of argument rearrangement ---

    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Preparing FUSE arguments with new_argc = " << new_argc << "." << std::endl;
    for(int i=0; i<new_argc; ++i) {
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: new_argv[" << i << "] = " << (new_argv[i] ? new_argv[i] : "null") << std::endl;
    }

    struct fuse_args args = FUSE_ARGS_INIT(new_argc, new_argv);

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
    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: About to call fuse_main with args.argc = " << args.argc << " args." << std::endl;
    for(int i=0; i<args.argc; ++i) {
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: fuse_main will see fuse_argv[" << i << "] = " << (args.argv[i] ? args.argv[i] : "null") << std::endl;
    }

    int fuse_ret = fuse_main(args.argc, args.argv, &simpli_ops, &fuse_data);

    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: fuse_main returned " << fuse_ret << "." << std::endl;
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] fuse_main returned " + std::to_string(fuse_ret) + ". Cleaning up.");

    fuse_opt_free_args(&args);
    delete[] new_argv;

    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: Exiting main() with code " << fuse_ret << "." << std::endl;

    return fuse_ret;
}

// Get FUSE private data (SimpliDfsFuseData)
static SimpliDfsFuseData* get_fuse_data() {
    fuse_context* context = fuse_get_context();
    if (!context) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] get_fuse_data: FUSE context is null.");
        return nullptr;
    }
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(context->private_data);
    if (!data) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] get_fuse_data: FUSE private_data is null.");
        return nullptr;
    }
    if (!data->metadata_client) {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] get_fuse_data: metadata_client within private_data is null. This may be expected if connection failed or after destroy.");
    }
    return data;
}

// FUSE destroy operation: Cleans up resources, like the metadata client
void simpli_destroy(void* private_data) {
    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy called." << std::endl;
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Entry.");
    if (!private_data) {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: private_data is null. No cleanup needed from here.");
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: private_data is null." << std::endl;
        return;
    }
    SimpliDfsFuseData* data = static_cast<SimpliDfsFuseData*>(private_data);
    if (data->metadata_client) {
        if (data->metadata_client->IsConnected()) {
            try {
                Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Attempting to disconnect metadata_client.");
                std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: Disconnecting metadata_client." << std::endl;
                data->metadata_client->Disconnect();
                Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Successfully disconnected from metaserver.");
                std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: Disconnected metadata_client." << std::endl;
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Exception during disconnect from metaserver: " + std::string(e.what()));
                std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: Exception during disconnect: " << e.what() << std::endl;
            }
        }
        delete data->metadata_client;
        data->metadata_client = nullptr;
        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Metadata client deleted.");
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: Metadata client deleted." << std::endl;
    } else {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: metadata_client was already null.");
        std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: metadata_client was already null." << std::endl;
    }
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_destroy: Exit.");
    std::cerr << getCurrentTimestamp() << " [FUSE_ADAPTER] DEBUG: simpli_destroy: Exiting function." << std::endl;
}

int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Entry for path: " + path_str);
    memset(stbuf, 0, sizeof(struct stat));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: FUSE data or metadata_client not available for path: " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Handled root path locally: " + path_str);
        return 0;
    }

    Message req_msg;
    req_msg._Type = MessageType::GetAttr;
    req_msg._Path = path_str;

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Failed to send GetAttr request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Received empty response for GetAttr request for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Metaserver returned error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }

        stbuf->st_mode = static_cast<mode_t>(res_msg._Mode);
        stbuf->st_uid = static_cast<uid_t>(res_msg._Uid);
        stbuf->st_gid = static_cast<gid_t>(res_msg._Gid);
        stbuf->st_size = static_cast<off_t>(res_msg._Size);
        stbuf->st_nlink = (S_ISDIR(stbuf->st_mode)) ? 2 : 1;
        stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL); // Placeholder

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Successfully processed " + path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_getattr: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

#ifdef SIMPLIDFS_HAS_STATX
int simpli_statx(const char *path, struct statx *stxbuf, int flags_unused, struct fuse_file_info *fi) {
    (void)fi; (void)flags_unused;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Entry for path: " + path_str);
    memset(stxbuf, 0, sizeof(struct statx));

    SimpliDfsFuseData* data = get_fuse_data();
     if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: FUSE data or metadata_client not available for path: " + path_str);
        return -EIO;
    }

    stxbuf->stx_uid = getuid();
    stxbuf->stx_gid = getgid();
    struct timespec current_time;
    clock_gettime(CLOCK_REALTIME, &current_time);
    stxbuf->stx_atime = stxbuf->stx_mtime = stxbuf->stx_ctime = stxbuf->stx_btime = current_time;

    if (path_str == "/") {
        stxbuf->stx_mode = S_IFDIR | 0755;
        stxbuf->stx_nlink = 2;
        stxbuf->stx_size = 4096;
        stxbuf->stx_attributes_mask |= STATX_ATTR_DIRECTORY;
        stxbuf->stx_mask |= STATX_BASIC_STATS | STATX_BTIME;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Handled root path: " + path_str);
        return 0;
    }

    Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_statx: Not fully implemented for non-root path: " + path_str + ". Returning ENOENT.");
    return -ENOENT;
}
#endif // SIMPLIDFS_HAS_STATX

int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset; (void) fi; (void)flags;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Entry for path: " + path_str);

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    filler(buf, ".", NULL, 0, (enum fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (enum fuse_fill_dir_flags)0);

    Message req_msg;
    req_msg._Type = MessageType::Readdir;
    req_msg._Path = path_str;

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Failed to send Readdir request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }

        std::istringstream name_stream(res_msg._Data);
        std::string name_token;
        while(std::getline(name_stream, name_token, ' ')) { // Corrected: char literal ' '
            if (!name_token.empty()) {
                filler(buf, name_token.c_str(), NULL, 0, (enum fuse_fill_dir_flags)0);
            }
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Successfully listed " + path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_readdir: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_open(const char *path, struct fuse_file_info *fi) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Entry for path: " + path_str + " with flags: " + std::to_string(fi->flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Write access denied for root directory /");
            return -EACCES;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Allowed read-only open for root directory /.");
        return 0;
    }

    Message req_msg;
    req_msg._Type = MessageType::Open;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(fi->flags);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Failed to send Open request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }
        fi->fh = 1;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Successfully opened " + path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }
    if (size == 0) return 0;

    Message req_msg;
    req_msg._Type = MessageType::Read;
    req_msg._Path = path_str;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Failed to send Read request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }

        size_t bytes_to_copy = std::min(size, static_cast<size_t>(res_msg._Data.length()));
        memcpy(buf, res_msg._Data.data(), bytes_to_copy);

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Successfully read " + std::to_string(bytes_to_copy) + " bytes from " + path_str);
        return static_cast<ssize_t>(bytes_to_copy);
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_access(const char *path, int mask) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Entry for path: " + path_str + " with mask: " + std::to_string(mask));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Root access check always returns success for now.");
        return 0;
    }

    Message req_msg;
    req_msg._Type = MessageType::Access;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(mask);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Failed to send Access request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        int result = (res_msg._ErrorCode == 0) ? 0 : -res_msg._ErrorCode;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Path " + path_str + " access result: " + std::to_string(result));
        return result;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_access: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    std::string path_str(path);
    std::ostringstream oss_create_log;
    oss_create_log << getCurrentTimestamp() << " [FUSE_ADAPTER] simpli_create: Entry for path: " << path_str
                   << " with mode: " << std::oct << mode << std::dec << ", flags: " << fi->flags;
    Logger::getInstance().log(LogLevel::DEBUG, oss_create_log.str());

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Cannot create file at root path /.");
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::CreateFile;
    req_msg._Path = path_str;
    req_msg._Mode = static_cast<uint32_t>(mode);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Failed to send CreateFile request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }

        fi->fh = 1;
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Successfully created " + path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }
    if (size == 0) return 0;

    Message req_msg;
    req_msg._Type = MessageType::Write;
    req_msg._Path = path_str;
    req_msg._Offset = static_cast<int64_t>(offset);
    req_msg._Size = static_cast<uint64_t>(size);
    req_msg._Data.assign(buf, size);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Failed to send Write request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }
        ssize_t bytes_written = static_cast<ssize_t>(res_msg._Size);
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Successfully wrote " + std::to_string(bytes_written) + " bytes to " + path_str);
        return bytes_written;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_unlink(const char *path) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Entry for path: " + path_str);

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    if (path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Cannot unlink root directory /.");
        return -EISDIR;
    }

    Message req_msg;
    req_msg._Type = MessageType::Unlink;
    req_msg._Path = path_str;

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Failed to send Unlink request for " + path_str);
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Received empty response for " + path_str);
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Metaserver error " + std::to_string(res_msg._ErrorCode) + " for " + path_str);
            return -res_msg._ErrorCode;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Successfully unlinked " + path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_rename(const char *from_path, const char *to_path, unsigned int flags) {
    (void)flags;
    std::string from_path_str(from_path);
    std::string to_path_str(to_path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Entry from: " + from_path_str + " to: " + to_path_str + " with flags: " + std::to_string(flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: FUSE data or metadata_client not available.");
        return -EIO;
    }

    if (from_path_str == "/" || to_path_str == "/") {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Cannot rename to or from root directory.");
        return -EBUSY;
    }

    Message req_msg;
    req_msg._Type = MessageType::Rename;
    req_msg._Path = from_path_str;
    req_msg._NewPath = to_path_str;

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Failed to send Rename request.");
            return -EIO;
        }

        std::vector<char> received_vector = data->metadata_client->Receive();
        if (received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Received empty response.");
            return -EIO;
        }
        std::string serialized_res(received_vector.begin(), received_vector.end());
        Message res_msg = Message::Deserialize(serialized_res);

        if (res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Metaserver error " + std::to_string(res_msg._ErrorCode));
            return -res_msg._ErrorCode;
        }
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Successfully renamed " + from_path_str + " to " + to_path_str);
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_rename: Exception: " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_release(const char *path, struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Entry for path: " + path_str + " with fi->fh: " + std::to_string(fi->fh) + ". No server action implemented yet.");
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Exit for path: " + path_str);
    return 0;
}

int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    (void)fi;
    std::string path_str(path);
    Logger& logger = Logger::getInstance();
    logger.log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_utimens: Entry for path: " + path_str);
    logger.log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_utimens: Operation not yet fully implemented for " + path_str + ". Optimistically succeeding.");
    return 0;
}
