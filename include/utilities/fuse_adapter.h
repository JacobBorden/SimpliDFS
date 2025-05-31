#pragma once
#ifndef SIMPLIDFS_FUSE_ADAPTER_H
#define SIMPLIDFS_FUSE_ADAPTER_H

#define FUSE_USE_VERSION 31 // Changed from 30 to 31

#include <fuse.h> // Should be included before system headers like stat.h if it defines them
#include "utilities/filesystem.h"
#include <string>
#include <vector> // Keep for now, might be useful elsewhere or by fuse.h
#include <set>    // For std::set

struct SimpliDfsFuseData {
    FileSystem* fs;
    std::set<std::string> known_files; // Using set for efficient lookup
};

// FUSE operation functions
int simpli_getattr(const char *path, struct stat *stbuf);
int simpli_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int simpli_open(const char *path, struct fuse_file_info *fi);
int simpli_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int simpli_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi);
int simpli_access(const char *path, int mask); // Added

#endif // SIMPLIDFS_FUSE_ADAPTER_H
