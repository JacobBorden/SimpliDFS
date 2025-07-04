#pragma once

#include <string>

namespace simplidfs {

void setVarDir(const std::string &dir);
const std::string &getVarDir();

std::string logsDir();
std::string fileMetadataPath();
std::string nodeRegistryPath();

} // namespace simplidfs
