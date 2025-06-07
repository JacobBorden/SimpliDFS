#include <gtest/gtest.h>
#include "repair/RepairWorker.h"

TEST(RepairWorker, HealsPartial) {
    NodeHealthCache cache(2,3,std::chrono::seconds(1));
    cache.recordSuccess("nodeB");
    cache.recordSuccess("nodeC");
    std::unordered_map<std::string, InodeEntry> table;
    table["file"].replicas = {"nodeA"};
    table["file"].partial = true;

    RepairWorker worker(table, cache, 3);
    worker.runOnce();

    EXPECT_FALSE(table["file"].partial);
    EXPECT_EQ(table["file"].replicas.size(), 3u);
}

TEST(RepairWorker, AddsMissingReplicas) {
    NodeHealthCache cache(2,3,std::chrono::seconds(1));
    cache.recordSuccess("nodeA");
    cache.recordSuccess("nodeB");
    cache.recordSuccess("nodeC");
    std::unordered_map<std::string, InodeEntry> table;
    table["file"].replicas = {"nodeA","nodeB"};

    RepairWorker worker(table, cache, 3);
    worker.runOnce();

    EXPECT_EQ(table["file"].replicas.size(), 3u);
    EXPECT_FALSE(table["file"].partial);
}
