#include "cluster/NodeHealthCache.h"
#include "utilities/metrics.h"
#include <algorithm>
#include <mutex>

NodeHealthCache::NodeHealthCache(std::size_t failureTh,
                                 std::size_t successTh,
                                 std::chrono::seconds cooldown)
    : failureThreshold_(failureTh),
      successThreshold_(successTh),
      cooldown_(cooldown) {}

static int toVal(NodeState s) {
    switch(s) {
        case NodeState::ALIVE: return 1;
        case NodeState::SUSPECT: return 2;
        case NodeState::DEAD: return 3;
    }
    return 0;
}

NodeState NodeHealthCache::state(const NodeID &id) const {
    std::lock_guard<std::mutex> lg(mutex_);
    auto it = map_.find(id);
    if (it == map_.end()) {
        MetricsRegistry::instance().setGauge("simplidfs_node_health", toVal(NodeState::ALIVE), {{"node", id}});
        return NodeState::ALIVE;
    }
    MetricsRegistry::instance().setGauge("simplidfs_node_health", toVal(it->second.state), {{"node", id}});
    return it->second.state;
}

void NodeHealthCache::recordSuccess(const NodeID &id) {
    std::lock_guard<std::mutex> lg(mutex_);
    auto &e = map_[id];
    auto now = SteadyClock::now();
    e.failures = 0;
    ++e.successes;
    if (e.state != NodeState::ALIVE) {
        if (e.state == NodeState::DEAD && (now - e.lastFailure) < cooldown_)
            return;
        if (e.successes >= successThreshold_) {
            e.state = NodeState::ALIVE;
            e.successes = 0;
            e.lastChange = now;
        }
    } else {
        e.lastChange = now;
        if (e.successes > successThreshold_) e.successes = successThreshold_;
    }
    MetricsRegistry::instance().setGauge("simplidfs_node_health", toVal(e.state), {{"node", id}});
    updateReplicationMetric();
}

void NodeHealthCache::recordFailure(const NodeID &id) {
    std::lock_guard<std::mutex> lg(mutex_);
    auto &e = map_[id];
    auto now = SteadyClock::now();
    e.successes = 0;
    if (e.state == NodeState::DEAD) {
        e.lastFailure = now;
        e.lastChange = now;
        return;
    }
    ++e.failures;
    if (e.failures >= failureThreshold_) {
        e.state = NodeState::DEAD;
        e.failures = 0;
        e.lastChange = now;
        e.lastFailure = now;
        MetricsRegistry::instance().incrementCounter(
            "simplidfs_replication_failures", 1.0, {{"node", id}});
    } else {
        e.state = NodeState::SUSPECT;
        e.lastChange = now;
    }
    MetricsRegistry::instance().setGauge("simplidfs_node_health", toVal(e.state), {{"node", id}});
    updateReplicationMetric();
}

std::vector<NodeID> NodeHealthCache::getHealthyNodes() const {
    std::lock_guard<std::mutex> lg(mutex_);
    std::vector<NodeID> result;
    result.reserve(map_.size());
    for (const auto &kv : map_) {
        if (kv.second.state == NodeState::ALIVE) {
            result.push_back(kv.first);
        }
    }
    return result;
}

std::unordered_map<NodeID, NodeHealthCache::StateInfo> NodeHealthCache::snapshot() const {
    std::lock_guard<std::mutex> lg(mutex_);
    std::unordered_map<NodeID, StateInfo> res;
    for (const auto &kv : map_) {
        res.emplace(kv.first, StateInfo{kv.second.state, kv.second.lastChange});
    }
    return res;
}

void NodeHealthCache::updateReplicationMetric() const {
    size_t pending = 0;
    for (const auto &kv : map_) {
        if (kv.second.state != NodeState::ALIVE) ++pending;
    }
    MetricsRegistry::instance().setGauge("simplidfs_replication_pending", static_cast<double>(pending),
                                         {{"component", "nodes"}});
}
