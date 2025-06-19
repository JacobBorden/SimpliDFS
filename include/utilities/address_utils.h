#pragma once
#ifndef SIMPLIDFS_ADDRESS_UTILS_H
#define SIMPLIDFS_ADDRESS_UTILS_H

#include <string>

/**
 * @brief Parse a string in the form "ip:port".
 *
 * Splits @p address around the colon and converts the port portion
 * to an integer.
 *
 * @param address Input string containing the ip and port.
 * @param ip Output parameter receiving the ip part on success.
 * @param port Output parameter receiving the port number on success.
 * @return True if @p address contains a valid "ip:port" pair,
 *         false otherwise.
 */
inline bool parseAddressPort(const std::string &address,
                             std::string &ip,
                             int &port) {
    size_t colonPos = address.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    ip = address.substr(0, colonPos);
    std::string portStr = address.substr(colonPos + 1);
    try {
        port = std::stoi(portStr);
        return true;
    } catch (...) {
        return false;
    }
}

#endif // SIMPLIDFS_ADDRESS_UTILS_H
