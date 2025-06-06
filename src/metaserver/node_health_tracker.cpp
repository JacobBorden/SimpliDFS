#include "metaserver/node_health_tracker.h"

NodeHealthTracker::NodeHealthTracker(std::chrono::seconds threshold)
    : deadThreshold_(threshold) {}

void NodeHealthTracker::recordSuccess(const std::string &nodeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastSuccess_[nodeId] = std::chrono::steady_clock::now();
}

bool NodeHealthTracker::isNodeDead(const std::string &nodeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lastSuccess_.find(nodeId);
    if (it == lastSuccess_.end()) {
        return false; // never seen -> treat as alive
    }
    return (std::chrono::steady_clock::now() - it->second) > deadThreshold_;
}

void NodeHealthTracker::setThreshold(std::chrono::seconds threshold) {
    deadThreshold_ = threshold;
}
