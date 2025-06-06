#include "cluster/NodeHealthCache.h"
#include <algorithm>

NodeHealthCache::NodeHealthCache(std::chrono::seconds suspect,
                                 std::chrono::seconds dead)
    : suspectTimeout(suspect), deadTimeout(dead) {}

void NodeHealthCache::updateState(const NodeID &id) const {
    auto it = map_.find(id);
    if (it == map_.end()) return;
    auto now = SteadyClock::now();
    Entry &e = const_cast<Entry &>(it->second);
    if (e.state == NodeState::SUSPECT && now - e.lastChange >= suspectTimeout) {
        e.state = NodeState::DEAD;
        e.lastChange = now;
    } else if (e.state == NodeState::DEAD && now - e.lastChange >= deadTimeout) {
        map_.erase(it);
    }
}

NodeState NodeHealthCache::state(const NodeID &id) const {
    updateState(id);
    auto it = map_.find(id);
    if (it == map_.end()) return NodeState::HEALTHY;
    return it->second.state;
}

void NodeHealthCache::recordSuccess(const NodeID &id) {
    Entry &e = map_[id];
    e.state = NodeState::HEALTHY;
    e.lastChange = SteadyClock::now();
}

void NodeHealthCache::recordFailure(const NodeID &id) {
    Entry &e = map_[id];
    auto now = SteadyClock::now();
    if (e.state == NodeState::DEAD) {
        e.lastChange = now;
    } else if (e.state == NodeState::SUSPECT &&
               now - e.lastChange >= suspectTimeout) {
        e.state = NodeState::DEAD;
        e.lastChange = now;
    } else {
        e.state = NodeState::SUSPECT;
        e.lastChange = now;
    }
}

std::vector<NodeID> NodeHealthCache::healthyNodes(size_t max) const {
    std::vector<NodeID> result;
    result.reserve(max);
    auto now = SteadyClock::now();
    for (auto it = map_.begin(); it != map_.end(); ) {
        Entry &e = const_cast<Entry &>(it->second);
        if (e.state == NodeState::SUSPECT && now - e.lastChange >= suspectTimeout) {
            e.state = NodeState::DEAD;
            e.lastChange = now;
        }
        if (e.state == NodeState::DEAD && now - e.lastChange >= deadTimeout) {
            it = map_.erase(it);
            continue;
        }
        if (e.state == NodeState::HEALTHY) {
            result.push_back(it->first);
            if (result.size() == max) break;
        }
        ++it;
    }
    return result;
}

std::unordered_map<NodeID, NodeHealthCache::StateInfo> NodeHealthCache::snapshot() const {
    std::unordered_map<NodeID, StateInfo> result;
    for (const auto &kv : map_) {
        result.emplace(kv.first, StateInfo{kv.second.state, kv.second.lastChange});
    }
    return result;
}

