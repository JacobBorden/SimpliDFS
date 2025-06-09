#include "utilities/fuse_adapter.h" // Should be first for precompiled headers if used
#include "utilities/logger.h"
#include "utilities/client.h" // For Networking::Client
#include "utilities/message.h" // For Message, MessageType
#include "utilities/metrics.h"
// Standard C++ headers
#include <iostream> // For std::cerr, std::endl (better than fprintf to stderr for C++)
#include <string>
#include <vector>
#include <stdexcept> // For std::stoi, std::invalid_argument, std::out_of_range
#include <algorithm> // For std::min
#include <sstream>   // For std::ostringstream, std::istringstream
#include <iomanip>   // For std::put_time
#include <chrono>    // For std::chrono::*
#include <unordered_map>
#include <memory>
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

// Helper function to parse "ip:port" string
static bool parse_ip_port(const std::string& addr_str, std::string& out_ip, int& out_port) {
    std::istringstream iss(addr_str);
    std::string segment;
    if (std::getline(iss, segment, ':')) {
        out_ip = segment;
        if (std::getline(iss, segment)) {
            try {
                out_port = std::stoi(segment);
                return true;
            } catch (const std::invalid_argument& ia) {
                Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] parse_ip_port: Invalid port format '" + segment + "' in address '" + addr_str + "'");
                return false;
            } catch (const std::out_of_range& oor) {
                Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] parse_ip_port: Port value out of range '" + segment + "' in address '" + addr_str + "'");
                return false;
            }
        }
    }
    Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] parse_ip_port: Could not parse IP and port from address '" + addr_str + "'");
    return false;
}

struct FuseLatency {
    std::string op;
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    FuseLatency(const std::string& o) : op(o) {}
    ~FuseLatency() {
        auto dur = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        MetricsRegistry::instance().observe("simplidfs_fuse_latency_seconds", dur, {{"op", op}});
    }
};

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
int simpli_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int simpli_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi);
int simpli_unlink(const char *path);
int simpli_rename(const char *from_path, const char *to_path, unsigned int flags);
int simpli_release(const char *path, struct fuse_file_info *fi);
int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi);
void simpli_destroy(void* private_data);

// Global map to serialize concurrent writes per file
static std::mutex g_lock_map_mutex;
static std::unordered_map<std::string, std::shared_ptr<std::mutex>> g_file_locks;

static std::shared_ptr<std::mutex> get_file_lock(const std::string& path) {
    std::lock_guard<std::mutex> guard(g_lock_map_mutex);
    auto it = g_file_locks.find(path);
    if (it == g_file_locks.end()) {
        it = g_file_locks.emplace(path, std::make_shared<std::mutex>()).first;
    }
    return it->second;
}

static bool ensure_metadata_connection_locked(SimpliDfsFuseData* data) {
    if (!data->metadata_client->IsConnected()) {
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] metadata client disconnected, attempting reconnect.");
        return data->metadata_client->connectWithRetry(data->metaserver_host.c_str(), data->metaserver_port);
    }
    return true;
}


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
    simpli_ops.truncate = simpli_truncate;
    simpli_ops.fallocate = simpli_fallocate;
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
    if (data && data->metadata_client) { // Added check for data itself
        std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
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

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }
    auto file_lock = get_file_lock(path_str);
    std::lock_guard<std::mutex> file_guard(*file_lock);
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
    // NOTE: statx is not in the list of functions to modify for this subtask.
    // However, if it were, the lock would go here:
    // std::lock_guard<std::mutex> lock(data->metadata_client_mutex);

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

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
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
    FuseLatency metric("open");
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Entry for path: " + path_str + " with flags: " + std::to_string(fi->flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
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
        // fi->fh is typically set by FUSE kernel module if not set by us and if open is successful.
        // If we needed to set it (e.g. fi->fh = some_custom_handle;), this is where it would be.
        // For now, we rely on FUSE's default fi->fh. If it's 0, that's problematic for map key.

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Metaserver Open successful for " + path_str + ". Now fetching node locations.");

        Message loc_req_msg;
        loc_req_msg._Type = MessageType::GetFileNodeLocationsRequest;
        loc_req_msg._Path = path_str;

        std::string serialized_loc_req = Message::Serialize(loc_req_msg);
        if (!data->metadata_client->Send(serialized_loc_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Failed to send GetFileNodeLocationsRequest for " + path_str);
            return -EIO; // Or a more specific error
        }

        std::vector<char> loc_received_vector = data->metadata_client->Receive();
        if (loc_received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Received empty response for GetFileNodeLocationsRequest for " + path_str);
            return -EIO;
        }
        std::string serialized_loc_res(loc_received_vector.begin(), loc_received_vector.end());
        Message loc_res_msg = Message::Deserialize(serialized_loc_res);

        if (loc_res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: GetFileNodeLocations failed for " + path_str + " with error: " + std::to_string(loc_res_msg._ErrorCode));
            return -loc_res_msg._ErrorCode; // Propagate error from metaserver
        }

        if (loc_res_msg._Data.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: No node locations received for " + path_str);
            return -ENOENT; // Or ENODATA if more appropriate
        }

        std::string first_node_addr_str;
        std::istringstream iss_nodes(loc_res_msg._Data);
        std::getline(iss_nodes, first_node_addr_str, ','); // Get the first address

        if (first_node_addr_str.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Parsed empty first node address for " + path_str);
            return -EIO;
        }

        std::string node_ip;
        int node_port;
        if (!parse_ip_port(first_node_addr_str, node_ip, node_port)) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Failed to parse node address '" + first_node_addr_str + "' for " + path_str);
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Attempting to connect to storage node " + node_ip + ":" + std::to_string(node_port) + " for file " + path_str);
        Networking::Client* storage_node_client = new Networking::Client();
        if (!storage_node_client->InitClientSocket() ||
            !storage_node_client->CreateClientTCPSocket(node_ip.c_str(), node_port) ||
            !storage_node_client->ConnectClientSocket()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Failed to connect to storage node " + node_ip + ":" + std::to_string(node_port) + " for " + path_str);
            delete storage_node_client;
            return -EHOSTUNREACH; // Or EIO
        }

        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Successfully connected to storage node " + node_ip + ":" + std::to_string(node_port) + ". Storing handle " + std::to_string(fi->fh));

        StorageNodeClient snc;
        snc.client = storage_node_client;
        snc.path = path_str;

        std::lock_guard<std::mutex> storage_lock(data->active_storage_clients_mutex);
        data->active_storage_clients[fi->fh] = snc;
        // Note: FUSE sets fi->fh to a unique value if we return 0 and don't set it ourselves,
        // unless FUSE_CAP_EXPLICIT_INVAL_DATA is set (which it isn't by default).
        // If fi->fh is 0 here, it means FUSE didn't assign one (e.g. because of direct_io or kernel_cache)
        // or the open wasn't truly "successful" in a way that FUSE assigns an fh.
        // This could be an issue if multiple files return fh=0. For now, assume fh is unique non-zero.
        if (fi->fh == 0) {
             Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: fi->fh is 0 after successful open and storage client connection for " + path_str + ". This might lead to issues if not unique.");
        }


        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Successfully opened " + path_str + " and connected to storage node.");
        return 0; // Success
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_open: Exception for " + path_str + ": " + std::string(e.what()));
        return -EIO;
    }
}

int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi;
    FuseLatency metric("read");
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_read: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }
    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }
    if (size == 0) return 0;

    Message req_msg;
    // Use legacy ReadFile message type expected by the current metaserver
    // implementation. MessageType::Read is not handled server-side yet.
    req_msg._Type = MessageType::ReadFile;
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

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
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
    FuseLatency metric("create");
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

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
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
        // Similar to open, fi->fh should be set by FUSE upon successful return from create.

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Metaserver CreateFile successful for " + path_str + ". Now fetching node locations.");

        Message loc_req_msg;
        loc_req_msg._Type = MessageType::GetFileNodeLocationsRequest;
        loc_req_msg._Path = path_str;

        std::string serialized_loc_req = Message::Serialize(loc_req_msg);
        if (!data->metadata_client->Send(serialized_loc_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Failed to send GetFileNodeLocationsRequest for " + path_str);
            // Attempt to unlink the metadata entry
            // TODO: Implement a helper for this cleanup logic if it becomes common
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -EIO;
        }

        std::vector<char> loc_received_vector = data->metadata_client->Receive();
        if (loc_received_vector.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Received empty response for GetFileNodeLocationsRequest for " + path_str);
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -EIO;
        }
        std::string serialized_loc_res(loc_received_vector.begin(), loc_received_vector.end());
        Message loc_res_msg = Message::Deserialize(serialized_loc_res);

        if (loc_res_msg._ErrorCode != 0) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: GetFileNodeLocations failed for " + path_str + " with error: " + std::to_string(loc_res_msg._ErrorCode));
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -loc_res_msg._ErrorCode;
        }

        if (loc_res_msg._Data.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: No node locations received for " + path_str);
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -ENOENT;
        }

        std::string first_node_addr_str;
        std::istringstream iss_nodes(loc_res_msg._Data);
        std::getline(iss_nodes, first_node_addr_str, ',');

        if (first_node_addr_str.empty()) {
             Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Parsed empty first node address for " + path_str);
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -EIO;
        }

        std::string node_ip;
        int node_port;
        if (!parse_ip_port(first_node_addr_str, node_ip, node_port)) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Failed to parse node address '" + first_node_addr_str + "' for " + path_str);
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -EIO;
        }

        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Attempting to connect to storage node " + node_ip + ":" + std::to_string(node_port) + " for file " + path_str);
        Networking::Client* storage_node_client = new Networking::Client();
        if (!storage_node_client->InitClientSocket() ||
            !storage_node_client->CreateClientTCPSocket(node_ip.c_str(), node_port) ||
            !storage_node_client->ConnectClientSocket()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Failed to connect to storage node " + node_ip + ":" + std::to_string(node_port) + " for " + path_str);
            delete storage_node_client;
            Message unlink_req_msg;
            unlink_req_msg._Type = MessageType::Unlink;
            unlink_req_msg._Path = path_str;
            data->metadata_client->Send(Message::Serialize(unlink_req_msg).c_str()); // Best effort unlink
            return -EHOSTUNREACH;
        }

        Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Successfully connected to storage node " + node_ip + ":" + std::to_string(node_port) + ". Storing handle " + std::to_string(fi->fh));

        StorageNodeClient snc;
        snc.client = storage_node_client;

        std::lock_guard<std::mutex> storage_lock(data->active_storage_clients_mutex);
        data->active_storage_clients[fi->fh] = snc;
         if (fi->fh == 0) {
             Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: fi->fh is 0 after successful create and storage client connection for " + path_str + ". This might lead to issues if not unique.");
        }

        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Successfully created " + path_str + " and connected to storage node.");
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_create: Exception for " + path_str + ": " + std::string(e.what()));
        // Consider attempting to unlink the metadata entry if an exception occurs after creation but before storage connection
        // For now, just return EIO.
        return -EIO;
    }
}

int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    FuseLatency metric("write");
    std::string path_str(path);
    SimpliDfsFuseData* data = get_fuse_data();
    if (path_str.empty() && data) {
        std::lock_guard<std::mutex> storage_lock(data->active_storage_clients_mutex);
        auto it = data->active_storage_clients.find(fi->fh);
        if (it != data->active_storage_clients.end()) {
            path_str = it->second.path;
        }
    }
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: Entry for path: " + path_str + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_write: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }
    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }
    auto file_lock = get_file_lock(path_str);
    std::lock_guard<std::mutex> file_guard(*file_lock);
    if (size == 0) return 0;

    Message req_msg;
    // Use legacy WriteFile message type expected by the current metaserver
    // implementation. The newer MessageType::Write is not yet supported
    // server-side, which caused "unknown message" errors during FUSE tests.
    req_msg._Type = MessageType::WriteFile;
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

int simpli_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    FuseLatency metric("truncate");
    std::string path_str(path);
    SimpliDfsFuseData* data = get_fuse_data();
    if (path_str.empty() && data) {
        std::lock_guard<std::mutex> guard(data->active_storage_clients_mutex);
        auto it = data->active_storage_clients.find(fi->fh);
        if (it != data->active_storage_clients.end()) {
            path_str = it->second.path;
        }
    }
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_truncate: Entry for path: " + path_str + " size: " + std::to_string(size));

    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_truncate: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }
    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
        return -EIO;
    }

    Message req_msg;
    req_msg._Type = MessageType::TruncateFile;
    req_msg._Path = path_str;
    req_msg._Size = static_cast<uint64_t>(size);

    try {
        std::string serialized_req = Message::Serialize(req_msg);
        if (!data->metadata_client->Send(serialized_req.c_str())) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_truncate: Failed to send request for " + path_str);
            return -EIO;
        }
        std::vector<char> recv_vec = data->metadata_client->Receive();
        if (recv_vec.empty()) {
            Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_truncate: Received empty response for " + path_str);
            return -EIO;
        }
        Message res = Message::Deserialize(std::string(recv_vec.begin(), recv_vec.end()));
        if (res._ErrorCode != 0) {
            return -res._ErrorCode;
        }
        return 0;
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_truncate: Exception for " + path_str + ": " + e.what());
        return -EIO;
    }
}

int simpli_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi) {
    (void)mode; // modes like FALLOC_FL_KEEP_SIZE not yet supported
    off_t end = offset + length;
    return simpli_truncate(path, end, fi);
}

int simpli_unlink(const char *path) {
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: Entry for path: " + path_str);

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->metadata_client) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_unlink: FUSE data or metadata_client not available for " + path_str);
        return -EIO;
    }

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
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
        {
            std::lock_guard<std::mutex> guard(g_lock_map_mutex);
            g_file_locks.erase(path_str);
        }
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

    std::lock_guard<std::mutex> lock(data->metadata_client_mutex);
    if (!ensure_metadata_connection_locked(data)) {
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
    std::string path_str(path);
    Logger::getInstance().log(LogLevel::INFO, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Entry for path: " + path_str + ", fh: " + std::to_string(fi->fh));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) {
        Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: FUSE data not available for " + path_str);
        return -EIO; // Or 0 if we want to be lenient on release
    }

    // Remove and disconnect the storage client associated with this file handle
    StorageNodeClient snc_to_remove;
    bool client_found = false;
    {
        std::lock_guard<std::mutex> storage_lock(data->active_storage_clients_mutex);
        auto it = data->active_storage_clients.find(fi->fh);
        if (it != data->active_storage_clients.end()) {
            snc_to_remove = it->second;
            data->active_storage_clients.erase(it);
            client_found = true;
        }
    }

    if (client_found && snc_to_remove.client) {
        Logger::getInstance().log(LogLevel::DEBUG, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Disconnecting and deleting storage client for fh: " + std::to_string(fi->fh) + " path: " + path_str);
        try {
            if (snc_to_remove.client->IsConnected()) { // Assuming IsConnected() exists
                 snc_to_remove.client->Disconnect();
            }
        } catch (const std::exception& e) {
             Logger::getInstance().log(LogLevel::ERROR, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: Exception during storage client disconnect for fh: " + std::to_string(fi->fh) + " path: " + path_str + " - " + e.what());
        }
        delete snc_to_remove.client;
    } else if (fi->fh != 0) { // Don't warn for fh=0 which might be for directories or failed opens
        Logger::getInstance().log(LogLevel::WARN, getCurrentTimestamp() + " [FUSE_ADAPTER] simpli_release: No active storage client found for fh: " + std::to_string(fi->fh) + " path: " + path_str);
    }

    // Note: The metadata server "close" or "release" message is not sent here.
    // This is typically handled by FUSE if direct_io is not used, or can be added if specific server-side release actions are needed.
    // For many distributed file systems, release on the client side just cleans up local resources.

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
