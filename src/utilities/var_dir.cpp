#include "utilities/var_dir.hpp"

#include <cstdlib>

namespace simplidfs {

static std::string varDir = [] {
  const char *env = std::getenv("SIMPLIDFS_VAR_DIR");
  if (env && env[0] != '\0') {
    return std::string(env);
  }
  return std::string("/var/simplidfs");
}();

void setVarDir(const std::string &dir) { varDir = dir; }

const std::string &getVarDir() { return varDir; }

std::string logsDir() { return getVarDir() + "/logs"; }

std::string fileMetadataPath() { return getVarDir() + "/file_metadata.dat"; }

std::string nodeRegistryPath() { return getVarDir() + "/node_registry.dat"; }

} // namespace simplidfs
