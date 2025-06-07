
/// \file
/// \brief Implementation of NodeHealthTracker

#include "metaserver/node_health_tracker.h"

NodeHealthTracker::NodeHealthTracker(std::chrono::seconds threshold)
    : deadThreshold_(threshold) {}

void NodeHealthTracker::recordSuccess(const std::string &nodeId) {
    // Protect shared state for concurrent callers
    std::lock_guard<std::mutex> lock(mutex_);
    // Record the timestamp of this successful interaction
    lastSuccess_[nodeId] = std::chrono::steady_clock::now();
}

bool NodeHealthTracker::isNodeDead(const std::string &nodeId) const {
    // Acquire lock before reading shared state
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lastSuccess_.find(nodeId);
    // If we have never communicated with this node, assume it is alive
    if (it == lastSuccess_.end()) {
        return false;
    }
    // Compute how long it has been since the last successful contact
    auto now = std::chrono::steady_clock::now();
    return (now - it->second) > deadThreshold_;
}

void NodeHealthTracker::setThreshold(std::chrono::seconds threshold) {
    // Update the duration after which nodes are considered dead
    deadThreshold_ = threshold;
}
