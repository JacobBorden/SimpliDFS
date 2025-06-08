#pragma once
#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>

/**
 * @brief Tracks last successful communication time with nodes.
 */
class NodeHealthTracker {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    /**
     * @brief Construct with the given dead threshold.
     * @param threshold Duration after which a node is considered dead.
     */
    explicit NodeHealthTracker(std::chrono::milliseconds threshold = std::chrono::seconds(30));

    /** Record a successful RPC to the given node. */
    void recordSuccess(const std::string &nodeId);

    /**
     * @brief Determine if a node is considered dead.
     * @param nodeId Identifier of the node.
     * @return True if no successful RPC within the threshold.
     */
    bool isNodeDead(const std::string &nodeId) const;

    /** Change the dead threshold. */
    void setThreshold(std::chrono::milliseconds threshold);

private:
    std::chrono::milliseconds deadThreshold_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TimePoint> lastSuccess_;
};
