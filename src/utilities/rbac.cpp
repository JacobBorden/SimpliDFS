#include "utilities/rbac.h"
#include <yaml-cpp/yaml.h>

using namespace SimpliDFS;

bool RBACPolicy::loadFromFile(const std::string &path) {
    try {
        YAML::Node config = YAML::LoadFile(path);
        if (config["roles"]) {
            for (auto it : config["roles"].as<std::map<std::string, YAML::Node>>()) {
                const auto &role = it.first;
                const auto &perms = it.second;
                for (const auto &perm : perms) {
                    rolePerms_[role].insert(perm.as<std::string>());
                }
            }
        }
        if (config["users"]) {
            for (auto it : config["users"].as<std::map<unsigned int, std::string>>()) {
                userRoles_[it.first] = it.second;
            }
        }
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool RBACPolicy::isAllowed(unsigned int uid, const std::string &operation) const {
    auto uIt = userRoles_.find(uid);
    if (uIt == userRoles_.end()) return false;
    auto rIt = rolePerms_.find(uIt->second);
    if (rIt == rolePerms_.end()) return false;
    return rIt->second.count(operation) > 0;
}
