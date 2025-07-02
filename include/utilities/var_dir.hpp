#pragma once
#include <cstdlib>
#include <string>

namespace simplidfs {

inline std::string getVarDir() {
    const char *env = std::getenv("SIMPLIDFS_VAR_DIR");
    if (env && env[0] != '\0') {
        return std::string(env);
    }
    return "/var/simplidfs";
}

inline std::string logsDir() { return getVarDir() + "/logs"; }
inline std::string fileMetadataPath() { return getVarDir() + "/file_metadata.dat"; }
inline std::string nodeRegistryPath() { return getVarDir() + "/node_registry.dat"; }

} // namespace simplidfs
