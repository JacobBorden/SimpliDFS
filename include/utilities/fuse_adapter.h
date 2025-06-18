#pragma once
#ifndef SIMPLIDFS_FUSE_ADAPTER_H
#define SIMPLIDFS_FUSE_ADAPTER_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define FUSE_USE_VERSION 316 // Changed from 30 to 31

#include <linux/stat.h>
#include <fuse3/fuse.h> // Should be included before system headers like stat.h if it defines them

// ------------------------------------------------------------------------
 // Fallbacks for hosts whose kernel headers are older than 5.19
 #ifndef STATX_ATTR_DIRECTORY
 #  define STATX_ATTR_DIRECTORY 0
 #endif
 #ifndef STATX_XATTR
 #  define STATX_XATTR          0
 #endif

#include "utilities/client.h" // Added
#include <string>
#include <vector> // Keep for now, might be useful elsewhere or by fuse.h
// #include <set> // Removed
#include <mutex>
#include <map>

// Structure to hold client connection to a storage node
struct StorageNodeClient {
    Networking::Client* client = nullptr;
    std::string path; ///< File path associated with this client
    // std::string node_id; // Optional: Could be added later if needed for logic/debugging
    // std::string node_address; // Optional: Could be added later
};

struct SimpliDfsFuseData {
    Networking::Client* metadata_client = nullptr;
    std::mutex metadata_client_mutex;
    std::string metaserver_host;
    int metaserver_port = 0;

    // Connections to storage nodes
    std::map<uint64_t, StorageNodeClient> active_storage_clients;
    std::mutex active_storage_clients_mutex;
};

// FUSE operation functions
void simpli_destroy(void* private_data); // Added declaration
int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int simpli_open(const char *path, struct fuse_file_info *fi);
int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
// int simpli_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi); // Removed for FUSE 3 compatibility
int simpli_access(const char *path, int mask); // Added
int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int simpli_truncate(const char *path, off_t size, struct fuse_file_info *fi);
int simpli_fallocate(const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi);
int simpli_unlink(const char *path);
int simpli_rename(const char *from, const char *to, unsigned int flags);
int simpli_release(const char *path, struct fuse_file_info *fi); // Added release
int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi); // Added utimens

/**
 * @brief Clamp negative offsets to zero.
 *
 * FUSE may sometimes pass a negative offset when the file position is
 * unknown. The metaserver expects non-negative offsets, so this helper
 * ensures we never send a negative value.
 *
 * @param offset Offset received from FUSE.
 * @return Zero if @p offset was negative, otherwise @p offset unchanged.
 */
inline off_t sanitize_offset(off_t offset) {
    return offset < 0 ? 0 : offset;
}

#ifdef SIMPLIDFS_HAS_STATX
struct statx; // Forward declaration for statx structure
// struct fuse_file_info; // Forward declaration for fuse_file_info structure - already declared by fuse.h inclusion
int simpli_statx(const char *path, struct statx *stxbuf, int flags, struct fuse_file_info *fi);
#endif

#endif // SIMPLIDFS_FUSE_ADAPTER_H
