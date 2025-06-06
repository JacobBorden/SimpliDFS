#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <mutex>

/**
 * @file NodeHealthCache.h
 * @brief Thread-safe liveness tracker used by the metadata layer.
 */

using NodeID = std::string;
using SteadyClock = std::chrono::steady_clock;

/**
 * @brief States reported by the health cache.
 */
enum class NodeState { ALIVE, SUSPECT, DEAD };

/**
 * @brief Tracks communication successes and failures for each node.
 */
class NodeHealthCache {
public:
    struct StateInfo { NodeState state; SteadyClock::time_point lastChange; };

    NodeHealthCache(std::size_t failureThreshold = 2,
                    std::size_t successThreshold = 3,
                    std::chrono::seconds cooldown = std::chrono::seconds(15));

    /** Get the current state of @p id. */
    NodeState state(const NodeID &id) const;

    /** Record a successful RPC to @p id. */
    void recordSuccess(const NodeID &id);

    /** Record a failed RPC to @p id. */
    void recordFailure(const NodeID &id);

    /** Return all IDs currently considered ALIVE. */
    std::vector<NodeID> getHealthyNodes() const;

    /** Snapshot of the internal map for diagnostics. */
    std::unordered_map<NodeID, StateInfo> snapshot() const;

private:
    struct Entry {
        NodeState state{NodeState::ALIVE};
        std::size_t failures{0};
        std::size_t successes{0};
        SteadyClock::time_point lastChange{SteadyClock::now()};
        SteadyClock::time_point lastFailure{SteadyClock::now()};
    };

    mutable std::mutex mutex_;
    mutable std::unordered_map<NodeID, Entry> map_;
    const std::size_t failureThreshold_;
    const std::size_t successThreshold_;
    const std::chrono::seconds cooldown_;
};

