#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

/**
 * @file NodeHealthCache.h
 * @brief Caches recent node health results for placement decisions.
 */

using NodeID = std::string;
using SteadyClock = std::chrono::steady_clock;

/**
 * @brief Represents the current observed state of a node.
 */
enum class NodeState { HEALTHY, SUSPECT, DEAD };

/**
 * @brief Tracks recent communication successes and failures with nodes.
 *
 * The cache promotes nodes from SUSPECT to DEAD when failures persist
 * beyond a timeout and eventually forgets about dead nodes so they can
 * be retried later.
 */
class NodeHealthCache {
public:
    struct StateInfo { NodeState state; SteadyClock::time_point lastChange; };

    explicit NodeHealthCache(std::chrono::seconds suspect = std::chrono::seconds(30),
                             std::chrono::seconds dead = std::chrono::seconds(90));
    /**
     * @brief Get the current state of a node.
     * @param id Node identifier.
     * @return The node's state.
     */
    NodeState state(const NodeID &id) const;

    /**
     * @brief Record a successful communication with a node.
     * @param id Node identifier.
     */
    void recordSuccess(const NodeID &id);

    /**
     * @brief Record a failed communication with a node.
     * @param id Node identifier.
     */
    void recordFailure(const NodeID &id);

    /**
     * @brief Return up to @p max node IDs that appear healthy.
     * @param max Maximum number of nodes to return.
     */
    std::vector<NodeID> healthyNodes(size_t max) const;

    std::unordered_map<NodeID, StateInfo> snapshot() const;

private:
    struct Entry { NodeState state; SteadyClock::time_point lastChange; };
    mutable std::unordered_map<NodeID, Entry> map_;
    std::chrono::seconds suspectTimeout;
    std::chrono::seconds deadTimeout;
    void updateState(const NodeID &id) const;
};

