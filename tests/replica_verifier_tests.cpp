#include <gtest/gtest.h>
#include "repair/ReplicaVerifier.h"
#include "utilities/metrics.h"

TEST(ReplicaVerifier, DetectsMismatch) {
    NodeHealthCache cache(2,3,std::chrono::seconds(1));
    cache.recordSuccess("A");
    cache.recordSuccess("B");
    cache.recordSuccess("C");

    std::unordered_map<std::string, InodeEntry> table;
    table["file"].replicas = {"A","B","C"};

    std::unordered_map<std::string,std::string> hashesA{{"file","h"}};
    std::unordered_map<std::string,std::string> hashesB{{"file","h"}};
    std::unordered_map<std::string,std::string> hashesC{{"file","x"}};

    auto fetcher = [&](const NodeID& id, const std::string& f) -> std::string {
        if(id=="A") return hashesA[f];
        if(id=="B") return hashesB[f];
        return hashesC[f];
    };

    ReplicaVerifier v(table, cache, fetcher);
    bool ok = v.verifyFile("file");
    EXPECT_FALSE(ok);
    EXPECT_TRUE(table["file"].partial);
}

TEST(ReplicaVerifier, RecordsFailureMetric) {
    MetricsRegistry::instance().reset();

    NodeHealthCache cache(2,3,std::chrono::seconds(1));
    cache.recordSuccess("A");
    cache.recordSuccess("B");
    cache.recordSuccess("C");

    std::unordered_map<std::string, InodeEntry> table;
    table["file"].replicas = {"A","B","C"};

    std::unordered_map<std::string,std::string> hashesA{{"file","h"}};
    std::unordered_map<std::string,std::string> hashesB{{"file","h"}};
    std::unordered_map<std::string,std::string> hashesC{{"file","x"}};

    auto fetcher = [&](const NodeID& id, const std::string& f) -> std::string {
        if(id=="A") return hashesA[f];
        if(id=="B") return hashesB[f];
        return hashesC[f];
    };

    ReplicaVerifier v(table, cache, fetcher);
    v.verifyFile("file");
    std::string metrics = MetricsRegistry::instance().toPrometheus();
    EXPECT_NE(metrics.find("simplidfs_replication_failures{file=\"file\"} 1"),
              std::string::npos);
}
