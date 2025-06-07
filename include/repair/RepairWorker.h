#pragma once
#include "cluster/NodeHealthCache.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <functional>

struct InodeEntry {
    std::vector<NodeID> replicas;
    bool partial{false};
};

class RepairWorker {
public:
    /**
     * @brief Construct a RepairWorker.
     * @param table Table of inode entries describing replicas.
     * @param cache Health cache providing node liveness information.
     * @param replicationFactor Desired number of replicas for each inode.
     * @param tick Interval between repair passes when running in the background.
     */
    RepairWorker(std::unordered_map<std::string, InodeEntry>& table,
                 NodeHealthCache& cache,
                 size_t replicationFactor = 3,
                 std::chrono::seconds tick = std::chrono::seconds(5),
                 std::function<void(const std::string&, const NodeID&, const NodeID&)> replicator = {})
        : table_(table), cache_(cache), replicationFactor_(replicationFactor), tick_(tick), replicator_(std::move(replicator)) {}

    ~RepairWorker();

    /** Start the background repair thread. */
    void start();
    /** Stop the background repair thread. */
    void stop();
    /** Perform a single repair iteration. */
    void runOnce();

private:
    void threadFunc();

    std::unordered_map<std::string, InodeEntry>& table_;
    NodeHealthCache& cache_;
    size_t replicationFactor_;
    std::chrono::seconds tick_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::function<void(const std::string&, const NodeID&, const NodeID&)> replicator_;
};
