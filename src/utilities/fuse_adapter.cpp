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
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr: Evaluating filename: " + filename);

    if (data->known_files.count(filename)) {
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_getattr: File found in known_files: " + filename);
        std::string content = data->fs->readFile(filename);
        stbuf->st_mode = S_IFREG | 0644; // RW for owner, R for group/other (simplification)
        // For a newly created file, mode would ideally come from 'create's mode argument
        // and umask. This is a simplified fixed permission.
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
        // Permissions are now 0644 from getattr.
        // The kernel will use these permissions based on getattr results.
        // We don't need to do an explicit O_ACCMODE check here for basic cases.
        // If open is for O_WRONLY or O_RDWR by the owner, it should be allowed at this stage.
        // Actual write permission will be checked by the kernel or by our simpli_write.
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_open: Opening existing file " + filename + " with flags: " + std::to_string(fi->flags) + ". Allowing, relying on getattr mode and write handler.");
        return 0; // Success, allow open
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

int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_create called for path: " + std::string(path) + " with mode: " + std::to_string(mode));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->fs) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_create: FUSE private_data not configured correctly.");
        return -EIO; // Input/output error
    }

    std::string spath(path);
    if (spath == "/") {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Cannot create directory / as a file.");
        return -EISDIR; // Is a directory
    }

    std::string filename = spath.substr(1); // Remove leading '/'

    Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Attempting to create or truncate: " + filename);

    // Attempt to create the file.
    // FileSystem::createFile is expected to return true if the file was newly created,
    // and false if it already existed or an error occurred.
    if (data->fs->createFile(filename)) {
        // File was newly created
        data->known_files.insert(filename);
        Logger::getInstance().log(LogLevel::INFO, "simpli_create: File successfully created (new): " + filename);

        // The mode is ignored for now as FileSystem doesn't store it.
        fi->fh = 1; // Set a dummy file handle for FUSE.
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Set fi->fh = " + std::to_string(fi->fh) + " for new file: " + filename);
        // Note: `fi->fh` could be set here if we were managing file handles directly in create.
        // For this system, `open` will likely follow and handle `fi->fh`.
        // The mode is ignored for now as FileSystem doesn't store it.

        return 0; // Success
    } else {
        // createFile returned false. This means either the file already existed,
        // or an error occurred during the creation attempt for a non-existent file.
        Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: createFile(" + filename + ") returned false. Assuming file may exist or creation failed.");

        // POSIX creat() truncates existing files. So, we attempt to truncate.
        // We need to be sure it's an "already exists" case vs. "creation failed for other reasons".
        // The current FileSystem::createFile doesn't distinguish.
        // We'll proceed with truncation attempt. If this also fails, it covers both scenarios.

        Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Attempting to truncate (overwrite with empty content) existing file: " + filename);
        if (data->fs->writeFile(filename, "")) {
            // Successfully truncated the file.
            Logger::getInstance().log(LogLevel::INFO, "simpli_create: Existing file successfully truncated: " + filename);
            // Ensure it's in known_files, as it might have existed in FS but not in our cache.
            if (!data->known_files.count(filename)) {
                data->known_files.insert(filename);
                Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Added truncated file " + filename + " to known_files.");
            }

            fi->fh = 1; // Set a dummy file handle for FUSE.
            Logger::getInstance().log(LogLevel::DEBUG, "simpli_create: Set fi->fh = " + std::to_string(fi->fh) + " for truncated file: " + filename);

            return 0; // Success
        } else {
            // Truncation failed.
            // This could be because:
            // 1. `createFile` failed because the file truly couldn't be created (e.g., bad path, FS error), AND it didn't exist.
            // 2. `createFile` indicated "already exists" (by returning false), but `writeFile` to truncate failed.
            Logger::getInstance().log(LogLevel::ERROR, "simpli_create: Failed to create (or createFile indicated no new creation) AND failed to truncate file: " + filename + ". This could be a genuine I/O error or permissions issue with the underlying FileSystem implementation.");
            return -EIO; // Input/output error seems appropriate for this combined failure.
        }
    }

    // The `mode` parameter contains requested permissions. We should store/apply them.
    // Our current FileSystem doesn't store modes. For now, this is a simplification.
    // getattr will return a default mode.

    // According to FUSE docs for `create`, if the call is successful,
    // `fi->fh` can be set to a file handle, and `fi->keep_cache` and `fi->nonseekable` can be set.
    // For this implementation, we assume that `open` will be called if these are needed.
    // Many applications will call `creat()` then `close()`, then `open()`.
    // FUSE handles setting up `fi->fh` if we return 0 from `create` and don't set it ourselves.
}

int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    (void)fi; // Mark as unused if not using file handle specific data yet
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_write called for path: " + std::string(path) + ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->fs) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_write: FUSE private_data not configured correctly.");
        return -EIO;
    }

    std::string filename = std::string(path).substr(1); // Remove leading '/'

    // Check if the file is known/exists.
    // Create or open(O_CREAT) should have made the file.
    if (!data->known_files.count(filename)) {
         // Double check with actual filesystem in case known_files is out of sync
        if (data->fs->readFile(filename) == "" && !data->fs->createFile(filename)) { // Attempt to create if read "" (non-existent)
             Logger::getInstance().log(LogLevel::ERROR, "simpli_write: Attempt to write to unknown or non-existent file and creation failed: " + filename);
             return -ENOENT; // No such file or directory
        }
        // If createFile succeeded or readFile found something (e.g. file existed but not in known_files)
        // Add to known_files to keep it consistent
        if (!data->known_files.count(filename)){
            data->known_files.insert(filename);
            Logger::getInstance().log(LogLevel::INFO, "simpli_write: File " + filename + " was not in known_files, added.");
        }
    }

    // Read current content
    std::string current_content = data->fs->readFile(filename);
    size_t current_len = current_content.length();

    // Prepare new content buffer
    // The final size could be offset + size, or current_len if offset + size is within current_len
    size_t new_len_estimate = std::max(current_len, (size_t)offset + size);
    std::string new_content_str;
    new_content_str.resize(new_len_estimate); // Resize and fill with nulls

    // Copy existing data up to offset
    size_t pre_offset_len = std::min((size_t)offset, current_len);
    for(size_t i=0; i < pre_offset_len; ++i) {
        new_content_str[i] = current_content[i];
    }

    // If offset is beyond current length, the gap is already filled with nulls by resize
    // Or, explicitly fill if resize doesn't guarantee nulls (it does for std::string)
    if ((size_t)offset > current_len) {
        for(size_t i = current_len; i < (size_t)offset; ++i) {
            new_content_str[i] = '\0';
        }
    }

    // Write new data from buf
    for(size_t i=0; i < size; ++i) {
        if ((size_t)offset + i < new_len_estimate) { // Boundary check
            new_content_str[(size_t)offset + i] = buf[i];
        } else {
            // This would happen if new_len_estimate was too small - implies an issue with logic or resize.
            // For safety, append. This part of code should ideally not be reached if resize is correct.
            new_content_str += buf[i];
        }
    }

    // If the write was within the old content and didn't reach its end, append the remainder of old content
    if ((size_t)offset + size < current_len) {
        for(size_t i = (size_t)offset + size; i < current_len; ++i) {
            // This character should already be in new_content_str if new_len_estimate >= current_len
            // However, if offset+size caused a shrink, and new_len_estimate was based on offset+size,
            // then we need to append the tail.
            // Current logic: new_len_estimate = max(current_len, offset+size).
            // So new_content_str is already large enough to hold the tail.
            // This loop just ensures those characters from current_content are preserved if not overwritten.
            new_content_str[i] = current_content[i];
        }
        // If new_len_estimate was offset + size, and this was smaller than current_len, we need to truncate.
        // The current resize() to new_len_estimate handles this.
         new_content_str.resize(new_len_estimate); // Ensure correct final size
    } else {
        // If write went up to or extended the file, new_len_estimate is offset + size.
        // String is already resized to this.
         new_content_str.resize((size_t)offset + size); // Ensure correct final size
    }


    if (data->fs->writeFile(filename, new_content_str)) {
        Logger::getInstance().log(LogLevel::INFO, "simpli_write: Successfully wrote " + std::to_string(size) + " bytes to " + filename + " at offset " + std::to_string(offset) + ". New size: " + std::to_string(new_content_str.length()));
        return size; // Return number of bytes written
    } else {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_write: FileSystem::writeFile failed for: " + filename);
        return -EIO;
    }
}

int simpli_unlink(const char *path) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_unlink called for path: " + std::string(path));

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->fs) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: FUSE private_data not configured correctly.");
        return -EIO;
    }

    std::string spath(path);
    if (spath == "/") {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_unlink: Cannot unlink root directory.");
        return -EISDIR; // Is a directory
    }

    std::string filename = spath.substr(1); // Remove leading '/'

    // No need to check known_files first, FileSystem::deleteFile handles non-existent files.
    // if (!data->known_files.count(filename)) {
    //     Logger::getInstance().log(LogLevel::WARN, "simpli_unlink: File not found in known_files: " + filename);
    //     // POSIX unlink returns ENOENT if it never existed.
    //     // FileSystem::deleteFile returns false if file doesn't exist, which we map to -ENOENT.
    // }

    if (data->fs->deleteFile(filename)) {
        Logger::getInstance().log(LogLevel::INFO, "simpli_unlink: File deleted successfully: " + filename);
        if (data->known_files.count(filename)) { // Only erase if it was there
            data->known_files.erase(filename); // Update our cache
        }
        return 0; // Success
    } else {
        // deleteFile returns false if the file didn't exist.
        Logger::getInstance().log(LogLevel::WARN, "simpli_unlink: FileSystem::deleteFile failed for " + filename + " (likely means file did not exist).");
        return -ENOENT; // No such file or directory
    }
}

int simpli_rename(const char *from, const char *to, unsigned int flags) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_rename called from: " + std::string(from) + " to: " + std::string(to) + " with flags: " + std::to_string(flags));

    // For now, we will ignore flags for simplicity, assuming basic rename behavior.
    // if (flags != 0) {
    //     Logger::getInstance().log(LogLevel::WARN, "simpli_rename: flags are not supported, returning EINVAL.");
    //     return -EINVAL;
    // }

    SimpliDfsFuseData* data = get_fuse_data();
    if (!data || !data->fs) {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: FUSE private_data not configured correctly.");
        return -EIO;
    }

    std::string sfrom(from);
    std::string sto(to);

    if (sfrom == "/" || sto == "/") {
        Logger::getInstance().log(LogLevel::ERROR, "simpli_rename: Cannot rename to or from root directory.");
        return -EBUSY;
    }

    std::string old_filename = sfrom.substr(1);
    std::string new_filename = sto.substr(1);

    if (data->fs->renameFile(old_filename, new_filename)) {
        Logger::getInstance().log(LogLevel::INFO, "simpli_rename: File renamed successfully from " + old_filename + " to " + new_filename);
        // Update known_files: remove old, insert new
        if (data->known_files.count(old_filename)) { // Should exist if renameFile succeeded
             data->known_files.erase(old_filename);
        }
        data->known_files.insert(new_filename);
        return 0; // Success
    } else {
        // FileSystem::renameFile logs "Attempted to rename non-existent file" or
        // "Attempted to rename to an already existing file".
        // We need to map this boolean 'false' to a FUSE error code.
        // A robust way would be for FileSystem::renameFile to return an enum or throw specific exceptions.
        // Lacking that, we make a best guess or return a generic error.

        // Check if the source file still exists. If not, it's ENOENT.
        // This is racy, but a common approach.
        // A less racy check is to see if FileSystem thinks the new file exists.
        // If new_filename is now in its _Files map, it means renameFile failed because new_filename existed.
        // If old_filename is not in _Files, it means renameFile failed because old_filename didn't exist.

        // The current FileSystem::renameFile implementation:
        // 1. Checks if old_filename exists. If not, logs WARN, returns false. -> maps to ENOENT
        // 2. Checks if new_filename exists. If yes, logs WARN, returns false. -> maps to EEXIST

        // We don't have direct access to _Files here.
        // We can try to mimic the checks if necessary, but it's not ideal.
        // For now, let's assume that if renameFile fails, it could be one of these two.
        // To differentiate, we could try a readFile on old_filename. If it's empty (or some error indicator),
        // it might be ENOENT. If readFile on new_filename is non-empty, it might be EEXIST.
        // This is still heuristic.

        // Given FileSystem::renameFile logs the specific reason, and for this exercise
        // we can't change FileSystem::renameFile easily to return better errors.
        // Let's assume a common default. ENOENT for source or EEXIST for target are common.
        // POSIX rename overwrites target files. Our FileSystem::renameFile does not.
        // So if new_filename exists, our renameFile fails, which is like RENAME_NOREPLACE.
        // In that case, EEXIST is appropriate.

        // Attempt to infer:
        // This is still not perfect because FileSystem::readFile itself returns "" for non-existent files.
        // A dedicated FileSystem::fileExists method would be better.
        // Since FileSystem::renameFile already logs the reason, we'll use a generic error here.
        // The prompt suggested -EPERM if specific cause is unknown.
        Logger::getInstance().log(LogLevel::WARN, "simpli_rename: FileSystem::renameFile failed for " + old_filename + " to " + new_filename + ". This could be due to source not existing or target already existing (and not overwriting).");

        // Let's try to be slightly more specific based on logs from FileSystem::renameFile (though not ideal to rely on logs for logic)
        // If we had a 'fileExists' method in FileSystem:
        // if (!data->fs->fileExists(old_filename)) return -ENOENT;
        // if (data->fs->fileExists(new_filename)) return -EEXIST; // if RENAME_NOREPLACE behavior

        // Given current constraints, -EPERM is a safe bet as per prompt.
        // However, let's try to infer slightly. If the new_filename now exists (somehow, though our rename doesn't overwrite),
        // that would be EEXIST. If old_filename doesn't exist, ENOENT.
        // This is hard without better FileSystem feedback.
        // The provided solution template leans towards -EPERM.

        return -EPERM; // General permission/operation not permitted error
    }
}

int simpli_release(const char *path, struct fuse_file_info *fi) {
    Logger::getInstance().log(LogLevel::DEBUG, "simpli_release called for path: " + std::string(path) + " with fi->fh: " + std::to_string(fi->fh));
    // This is the counterpart to 'open' or 'create'.
    // For 'create', fi->fh was set (e.g., to 1).
    // For 'open', fi->fh might be 0 if not set by 'open' itself, or some other value.
    // Since we are using a dummy file handle and not managing complex per-handle state,
    // there's nothing specific to clean up here based on fh.
    // If fi->fh was used to store a resource index or pointer, this is where it would be freed.
    // For now, just logging is sufficient.
    return 0; // Success
}

// Stub for utimens
int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
    // fi can be NULL if utimensat(2) was called with AT_SYMLINK_NOFOLLOW.
    // We don't use fi for this stub anyway.
    (void)fi;

    Logger& logger = Logger::getInstance();
    logger.log(LogLevel::DEBUG, "simpli_utimens called for path: " + std::string(path));

    if (tv == nullptr) {
        logger.log(LogLevel::DEBUG, "simpli_utimens: tv is NULL (touch behavior, set to current time).");
        // This case means "set to current time".
        // Our FileSystem model doesn't store timestamps, so getattr always returns current time.
        // Thus, doing nothing here effectively achieves the desired outcome for this case.
    } else {
        // Log the requested timestamps.
        // tv[0] is atime (access time), tv[1] is mtime (modification time).
        // Special UTIME_NOW and UTIME_OMIT values could also be present in tv[n].tv_nsec.
        std::string atime_str = "atime: sec=" + std::to_string(tv[0].tv_sec) + " nsec=" + std::to_string(tv[0].tv_nsec);
        std::string mtime_str = "mtime: sec=" + std::to_string(tv[1].tv_sec) + " nsec=" + std::to_string(tv[1].tv_nsec);
        logger.log(LogLevel::DEBUG, "simpli_utimens: " + atime_str + ", " + mtime_str);

        if (tv[0].tv_nsec == UTIME_OMIT && tv[1].tv_nsec == UTIME_OMIT) {
            logger.log(LogLevel::DEBUG, "simpli_utimens: Both atime and mtime are UTIME_OMIT. No change needed.");
        } else if (tv[0].tv_nsec == UTIME_NOW) {
            logger.log(LogLevel::DEBUG, "simpli_utimens: atime is UTIME_NOW.");
        } else if (tv[0].tv_nsec == UTIME_OMIT) {
            logger.log(LogLevel::DEBUG, "simpli_utimens: atime is UTIME_OMIT.");
        }

        if (tv[1].tv_nsec == UTIME_NOW) {
            logger.log(LogLevel::DEBUG, "simpli_utimens: mtime is UTIME_NOW.");
        } else if (tv[1].tv_nsec == UTIME_OMIT) {
            logger.log(LogLevel::DEBUG, "simpli_utimens: mtime is UTIME_OMIT.");
        }
    }

    // Our FileSystem doesn't currently store timestamps for files.
    // simpli_getattr always reports the current time.
    // So, for now, we don't need to do anything to the underlying storage.
    // We return 0 to indicate success, which should satisfy `touch`.
    logger.log(LogLevel::INFO, "simpli_utimens: Operation completed successfully (stubbed) for " + std::string(path));
    return 0;
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
    simpli_ops.create  = simpli_create; // Add this line
    simpli_ops.write   = simpli_write; // Add this line
    simpli_ops.unlink  = simpli_unlink; // Add this line
    simpli_ops.rename  = simpli_rename; // Add this line
    simpli_ops.release = simpli_release; // Added release handler
    simpli_ops.utimens = simpli_utimens; // Added utimens handler
    // For FUSE 3.0+, you might also consider init/destroy if needed, but not for this minimal example.
    // simpli_ops.init = simpli_init; // Example
    // simpli_ops.destroy = simpli_destroy; // Example

    Logger::getInstance().log(LogLevel::INFO, "Passing control to fuse_main.");

    // fuse_main will take over the current thread and only return on unmount or error.
    // It parses FUSE options from argc/argv (e.g., -f for foreground, -d for debug, mountpoint).
    // The mountpoint is typically the first non-option argument.
    return fuse_main(argc, argv, &simpli_ops, &fuse_data); // Pass our fuse_data as user_data
}
