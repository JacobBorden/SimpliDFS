#pragma once
#include "cluster/NodeHealthCache.h"
#include <unordered_map>
#include <string>
#include <vector>

struct InodeEntry {
    std::vector<NodeID> replicas;
    bool partial{false};
};

class RepairWorker {
public:
    RepairWorker(std::unordered_map<std::string, InodeEntry>& table,
                 NodeHealthCache& cache,
                 size_t replicationFactor = 3)
        : table_(table), cache_(cache), replicationFactor_(replicationFactor) {}

    void runOnce();

private:
    std::unordered_map<std::string, InodeEntry>& table_;
    NodeHealthCache& cache_;
    size_t replicationFactor_;
};
