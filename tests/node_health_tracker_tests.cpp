#include <gtest/gtest.h>
#include "metaserver/node_health_tracker.h"
#include <thread>
#include <chrono>

TEST(NodeHealthTracker, AliveAfterSuccess) {
    NodeHealthTracker tracker(std::chrono::milliseconds(1000));
    tracker.recordSuccess("node1");
    EXPECT_FALSE(tracker.isNodeDead("node1"));
}

TEST(NodeHealthTracker, DeadAfterThreshold) {
    NodeHealthTracker tracker(std::chrono::milliseconds(500));
    tracker.recordSuccess("node1");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(tracker.isNodeDead("node1"));
}

TEST(NodeHealthTracker, ThresholdUpdate) {
    NodeHealthTracker tracker(std::chrono::milliseconds(2000));
    tracker.recordSuccess("node1");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    tracker.setThreshold(std::chrono::milliseconds(500));
    EXPECT_TRUE(tracker.isNodeDead("node1"));
}
