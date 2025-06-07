#pragma once
#ifndef SIMPLIDFS_RBAC_H
#define SIMPLIDFS_RBAC_H

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace SimpliDFS {

/**
 * @brief Manages role based access control using a YAML policy file.
 */
class RBACPolicy {
public:
    /**
     * @brief Load policy from a YAML file.
     * @param path Path to YAML file.
     * @return True on success.
     */
    bool loadFromFile(const std::string &path);

    /**
     * @brief Check if a user ID may perform an operation.
     * @param uid User ID from the request.
     * @param operation Name of the operation (e.g. "create", "read").
     * @return True if allowed.
     */
    bool isAllowed(unsigned int uid, const std::string &operation) const;

private:
    std::unordered_map<unsigned int, std::string> userRoles_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rolePerms_;
};

} // namespace SimpliDFS

#endif // SIMPLIDFS_RBAC_H
