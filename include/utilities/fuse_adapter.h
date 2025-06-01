#pragma once
#ifndef SIMPLIDFS_FUSE_ADAPTER_H
#define SIMPLIDFS_FUSE_ADAPTER_H

#define _GNU_SOURCE
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

#include "utilities/filesystem.h"
#include <string>
#include <vector> // Keep for now, might be useful elsewhere or by fuse.h
#include <set>    // For std::set

struct SimpliDfsFuseData {
    FileSystem* fs;
    std::set<std::string> known_files; // Using set for efficient lookup
};

// FUSE operation functions
int simpli_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags);
int simpli_open(const char *path, struct fuse_file_info *fi);
int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
// int simpli_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi); // Removed for FUSE 3 compatibility
int simpli_access(const char *path, int mask); // Added
int simpli_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int simpli_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int simpli_unlink(const char *path);
int simpli_rename(const char *from, const char *to, unsigned int flags);
int simpli_release(const char *path, struct fuse_file_info *fi); // Added release
int simpli_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi); // Added utimens

#endif // SIMPLIDFS_FUSE_ADAPTER_H
