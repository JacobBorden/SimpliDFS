#include "repair/RepairWorker.h"
#include <algorithm>

RepairWorker::~RepairWorker() { stop(); }

void RepairWorker::start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&RepairWorker::threadFunc, this);
}

void RepairWorker::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void RepairWorker::threadFunc() {
    while (running_) {
        runOnce();
        for (std::chrono::seconds s{0}; s < tick_ && running_; s += std::chrono::seconds(1)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void RepairWorker::runOnce() {
    auto candidates = cache_.getHealthyNodes();
    for (auto &kv : table_) {
        const std::string &fname = kv.first;
        auto &inode = kv.second;
        inode.replicas.erase(
            std::remove_if(inode.replicas.begin(), inode.replicas.end(),
                           [&](const NodeID &id) { return cache_.state(id) != NodeState::ALIVE; }),
            inode.replicas.end());

        bool hadPartial = inode.partial;
        size_t missing = replicationFactor_ > inode.replicas.size()
                             ? replicationFactor_ - inode.replicas.size()
                             : 0;

        // Add replicas on healthy nodes
        for (const auto &n : candidates) {
            if (missing == 0)
                break;
            if (std::find(inode.replicas.begin(), inode.replicas.end(), n) == inode.replicas.end()) {
                if (!inode.replicas.empty() && replicator_) {
                    replicator_(fname, inode.replicas.front(), n);
                }
                inode.replicas.push_back(n);
                --missing;
            }
        }

        if (hadPartial && inode.replicas.size() > 1 && replicator_) {
            for (size_t i = 1; i < inode.replicas.size(); ++i) {
                replicator_(fname, inode.replicas.front(), inode.replicas[i]);
            }
        }

        inode.partial = inode.replicas.size() < replicationFactor_;
    }
}
