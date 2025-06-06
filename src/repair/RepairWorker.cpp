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
        auto &inode = kv.second;
        if (!inode.partial)
            continue;
        size_t missing = replicationFactor_ > inode.replicas.size()
                             ? replicationFactor_ - inode.replicas.size()
                             : 0;
        for (const auto &n : candidates) {
            if (missing == 0)
                break;
            if (std::find(inode.replicas.begin(), inode.replicas.end(), n) == inode.replicas.end()) {
                inode.replicas.push_back(n);
                --missing;
            }
        }
        if (inode.replicas.size() >= replicationFactor_)
            inode.partial = false;
    }
}
