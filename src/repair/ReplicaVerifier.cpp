#include "repair/ReplicaVerifier.h"
#include "utilities/metrics.h"
#include <algorithm>

bool ReplicaVerifier::verifyFile(const std::string& filename) {
    auto it = table_.find(filename);
    if (it == table_.end()) return true;
    auto& entry = it->second;

    // Filter to healthy replicas only
    std::vector<NodeID> healthy;
    for (const auto& id : entry.replicas) {
        if (cache_.state(id) == NodeState::ALIVE) healthy.push_back(id);
    }

    if (healthy.empty()) {
        entry.partial = true;
        MetricsRegistry::instance().incrementCounter(
            "simplidfs_replication_failures", 1.0, {{"file", filename}});
        return false;
    }

    std::string refHash;
    bool first = true;
    bool mismatch = false;
    for (const auto& id : healthy) {
        std::string h = fetcher_(id, filename);
        if (first) {
            refHash = h;
            first = false;
        } else if (h != refHash) {
            mismatch = true;
        }
    }
    if (mismatch) {
        entry.partial = true;
        MetricsRegistry::instance().incrementCounter(
            "simplidfs_replication_failures", 1.0, {{"file", filename}});
    }
    MetricsRegistry::instance().setGauge("simplidfs_replica_healthy", mismatch ? 0 : 1, {{"file", filename}});
    return !mismatch;
}

void ReplicaVerifier::verifyAll() {
    size_t pending = 0;
    for (const auto& kv : table_) {
        if (!verifyFile(kv.first)) ++pending;
    }
    MetricsRegistry::instance().setGauge("simplidfs_replication_pending", static_cast<double>(pending),
                                         {{"component", "files"}});
}

