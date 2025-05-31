#include "utilities/fuse_adapter.h"
#include "utilities/logger.h"
#include <errno.h>
#include <string.h> // For memset, strerror, strcmp
#include <unistd.h> // for getuid, getgid
#include <time.h>   // for time()
#include <sys/stat.h> // For S_IFDIR, S_IFREG modes

// Helper to get our FileSystem instance and SimpliDfsFuseData from FUSE context
static SimpliDfsFuseData* get_fuse_data() {
    SimpliDfsFuseData* data = (SimpliDfsFuseData*) fuse_get_context()->private_data;
    if (!data || !data->fs) {
        Logger::getInstance().log(LogLevel::ERROR, "FUSE private_data not configured correctly or FileSystem not accessible.");
        return nullptr;
    }
    return data;
}

int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)fi; // Mark as unused if the function body does not use it
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr called for path: " + std::string(path));
    memset(stbuf, 0, sizeof(struct stat));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO;

    std::string spath(path);

    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = time(NULL);

    if (spath == "/") {
        stbuf->st_mode = S_IFDIR | 0755; // Read/execute permissions
        stbuf->st_nlink = 2;
        return 0;
    }

    std::string filename = spath.substr(1);
    if (data->known_files.count(filename)) {
        std::string content = data->fs->readFile(filename);
        stbuf->st_mode = S_IFREG | 0444; // Read-only permissions
        stbuf->st_nlink = 1;
        stbuf->st_size = content.length();
        return 0;
    }

    Logger::getInstance().log(LogLevel::WARN, "getattr: File not found in known_files: " + filename);
    return -ENOENT;
}

int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void)flags; // Mark as unused
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_readdir called for path: " + std::string(path));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO;

    if (strcmp(path, "/") != 0) {
        Logger::getInstance().log(LogLevel::ERROR, "readdir: Path is not root: " + std::string(path));
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0, (enum fuse_fill_dir_flags)0);
    filler(buf, "..", NULL, 0, (enum fuse_fill_dir_flags)0);

    for (const auto& filename_entry : data->known_files) {
        filler(buf, filename_entry.c_str(), NULL, 0, (enum fuse_fill_dir_flags)0);
    }
    return 0;
}

int simpli_open(const char *path, struct fuse_file_info *fi) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_open called for path: " + std::string(path) + " with flags: " + std::to_string(fi->flags));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO;

    std::string spath(path);
    if (spath == "/") {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
             Logger::getInstance().log(LogLevel::WARN, "simpli_open: Write access denied for directory /");
             return -EACCES;
        }
        return 0;
    }

    std::string filename = spath.substr(1);
    if (data->known_files.count(filename)) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) {
            Logger::getInstance().log(LogLevel::WARN, "simpli_open: Write access denied for " + filename);
            return -EACCES;
        }
        return 0;
    }

    Logger::getInstance().log(LogLevel::WARN, "simpli_open: File not found: " + filename);
    return -ENOENT;
}

int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void) fi;
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_read called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO;

    std::string filename = std::string(path).substr(1);

    if (!data->known_files.count(filename)) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_read: File not in known_files (should have been caught by open): " + filename);
        return -ENOENT;
    }

    std::string content = data->fs->readFile(filename);
    size_t content_len = content.length();

    if ((size_t)offset >= content_len) {
        return 0;
    }

    size_t read_len = content_len - offset;
    if (read_len > size) {
        read_len = size;
    }

    memcpy(buf, content.c_str() + offset, read_len);
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_read: Read " + std::to_string(read_len) + " bytes from " + filename);
    return read_len;
}

// int simpli_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
// (void)fi; // fi might be unused if not storing special per-handle info
// Logger::getInstance().log(LogLevel::DEBUG, "simpli_fgetattr called for path: " + std::string(path));
// // Most simple fgetattr implementations are identical to getattr, unless
// // you have per-file-handle state that would change attributes (e.g. size for append-only files)
// return simpli_getattr(path, stbuf);
// }

int simpli_access(const char *path, int mask) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_access called for path: " + std::string(path) + " with mask: " + std::to_string(mask));
    // For testing, let's be very permissive.
    // Existence (F_OK) is implicitly handled by returning 0 if we don't return ENOENT.
    // If the path (after removing leading '/') is in known_files or is "/", assume OK.

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data) return -EIO; // Should not happen

    std::string spath(path);
    if (spath == "/") {
        return 0; // Root is always accessible
    }

    std::string filename = spath.substr(1);
    if (data->known_files.count(filename)) {
        return 0; // File exists, grant access for testing
    }

    return -ENOENT;
}

// main function for the FUSE adapter
int main(int argc, char *argv[]) {
    // Initialize the logger first
    try {
        Logger::init("fuse_adapter_main.log", LogLevel::DEBUG); // Or another appropriate log file and level
    } catch (const std::exception& e) {
        // Cannot use logger here if it failed to initialize
        fprintf(stderr, "FATAL: Failed to initialize logger: %s\n", e.what());
        return 1; // Indicate critical error
    }

    Logger::getInstance().log(LogLevel::INFO, "Starting SimpliDFS FUSE adapter.");

    // --- Argument Parsing for Mount Point ---
    if (argc < 2) {
        Logger::getInstance().log(LogLevel::FATAL, "Usage: " + std::string(argv[0]) + " <mountpoint> [FUSE options]");
        // FUSE typically prints its own help message if -h or --help is passed,
        // or if it fails to parse args. fuse_main will handle this.
        // We might not need this explicit check if fuse_main's behavior is sufficient.
        // For now, let's keep it as a basic guard.
    }
    // The actual mountpoint argument is processed by fuse_main.

    // --- Initialize FileSystem and SimpliDfsFuseData ---
    FileSystem local_fs;
    SimpliDfsFuseData fuse_data;
    fuse_data.fs = &local_fs;
    // fuse_data.known_files is default constructed (empty set)

    // --- Pre-populate FileSystem with some test data ---
    // And update known_files in fuse_data accordingly.
    std::string file1_name = "hello.txt";
    std::string file1_content = "Hello from SimpliDFS FUSE!";
    if (local_fs.createFile(file1_name)) {
        if (local_fs.writeFile(file1_name, file1_content)) {
            fuse_data.known_files.insert(file1_name);
            Logger::getInstance().log(LogLevel::INFO, "Created test file: " + file1_name);
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "Failed to write to test file: " + file1_name);
        }
    } else {
        Logger::getInstance().log(LogLevel::ERROR, "Failed to create test file: " + file1_name);
    }

    std::string file2_name = "empty_file.txt"; // Renamed to avoid confusion if "empty.txt" is a common ignore pattern
    if (local_fs.createFile(file2_name)) {
        if (local_fs.writeFile(file2_name, "")) { // Empty content
            fuse_data.known_files.insert(file2_name);
            Logger::getInstance().log(LogLevel::INFO, "Created test file: " + file2_name);
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "Failed to write to test file: " + file2_name);
        }
    } else {
        Logger::getInstance().log(LogLevel::ERROR, "Failed to create test file: " + file2_name);
    }

    std::string file3_name = "data.log"; // Another test file
    std::string file3_content = "Log entry 1\nLog entry 2\nEnd of log.\n";
    if (local_fs.createFile(file3_name)) {
        if (local_fs.writeFile(file3_name, file3_content)) {
            fuse_data.known_files.insert(file3_name);
            Logger::getInstance().log(LogLevel::INFO, "Created test file: " + file3_name);
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "Failed to write to test file: " + file3_name);
        }
    } else {
        Logger::getInstance().log(LogLevel::ERROR, "Failed to create test file: " + file3_name);
    }

    // --- Define FUSE operations ---
    struct fuse_operations simpli_ops;
    memset(&simpli_ops, 0, sizeof(struct fuse_operations));

    simpli_ops.getattr = simpli_getattr;
    simpli_ops.readdir = simpli_readdir;
    simpli_ops.open    = simpli_open;
    simpli_ops.read    = simpli_read;
    // simpli_ops.fgetattr = simpli_fgetattr; // Removed for FUSE 3 compatibility
    simpli_ops.access = simpli_access; // Added
    // For FUSE 3.0+, you might also consider init/destroy if needed, but not for this minimal example.
    // simpli_ops.init = simpli_init; // Example
    // simpli_ops.destroy = simpli_destroy; // Example

    Logger::getInstance().log(LogLevel::INFO, "Passing control to fuse_main.");

    // fuse_main will take over the current thread and only return on unmount or error.
    // It parses FUSE options from argc/argv (e.g., -f for foreground, -d for debug, mountpoint).
    // The mountpoint is typically the first non-option argument.
    return fuse_main(argc, argv, &simpli_ops, &fuse_data); // Pass our fuse_data as user_data
}
