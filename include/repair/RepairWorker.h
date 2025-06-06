#pragma once
#include "cluster/NodeHealthCache.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

struct InodeEntry {
    std::vector<NodeID> replicas;
    bool partial{false};
};

class RepairWorker {
public:
    RepairWorker(std::unordered_map<std::string, InodeEntry>& table,
                 NodeHealthCache& cache,
                 size_t replicationFactor = 3,
                 std::chrono::seconds tick = std::chrono::seconds(5))
        : table_(table), cache_(cache), replicationFactor_(replicationFactor), tick_(tick) {}

    ~RepairWorker();

    void start();
    void stop();
    void runOnce();

private:
    void threadFunc();

    std::unordered_map<std::string, InodeEntry>& table_;
    NodeHealthCache& cache_;
    size_t replicationFactor_;
    std::chrono::seconds tick_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};
