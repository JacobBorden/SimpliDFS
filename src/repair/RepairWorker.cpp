#include "repair/RepairWorker.h"
#include <algorithm>

void RepairWorker::runOnce() {
    auto candidates = cache_.healthyNodes(replicationFactor_ * 2);
    for (auto& kv : table_) {
        auto& inode = kv.second;
        if (!inode.partial)
            continue;
        size_t missing = 0;
        if (inode.replicas.size() < replicationFactor_)
            missing = replicationFactor_ - inode.replicas.size();
        for (const auto& n : candidates) {
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
