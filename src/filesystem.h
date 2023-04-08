#pragma once
#ifndef _SIMPLIDFS_FILESYSTEM_H
#define _SIMPLIDFS_FILESYSTEM_H

#include <string>
#include <unordered_map>
#include <mutex>

class FileSystem {
public:
bool createFile(const std::string& _pFilename);
bool writeFile(const std::string& _pFilename, const std::string& _pContent);
std::string readFile(const std::string& _pFilename);
private:
std::unordered_map<std::string, std::string> _Files;
std::mutex _Mutex;
};


#endif
