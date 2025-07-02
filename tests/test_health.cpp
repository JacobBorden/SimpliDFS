#define CATCH_CONFIG_MAIN
#include "cluster/NodeHealthCache.h"
#include "metaserver/metaserver.h"
#include "repair/RepairWorker.h"
#include <algorithm>
#include <catch2/catch_all.hpp>

TEST_CASE("Node transitions based on successes and failures") {
  NodeHealthCache cache(2, 3, std::chrono::seconds(1));
  const std::string id = "N";
  cache.recordFailure(id);
  REQUIRE(cache.state(id) == NodeState::SUSPECT);
  cache.recordFailure(id);
  REQUIRE(cache.state(id) == NodeState::DEAD);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  cache.recordSuccess(id);
  cache.recordSuccess(id);
  cache.recordSuccess(id);
  REQUIRE(cache.state(id) == NodeState::ALIVE);
}

TEST_CASE("pickLiveNodes filters dead nodes") {
  MetadataManager mm;
  mm.registerNode("A", "127.0.0.1", 1001);
  mm.registerNode("B", "127.0.0.1", 1002);
  mm.healthCache().recordFailure("B");
  mm.healthCache().recordFailure("B");
  auto nodes = mm.pickLiveNodes(2);
  REQUIRE(std::find(nodes.begin(), nodes.end(), "B") == nodes.end());
}

TEST_CASE("RepairWorker heals partial replicas") {
  NodeHealthCache cache(2, 3, std::chrono::seconds(1));
  cache.recordSuccess("nodeB");
  cache.recordSuccess("nodeC");
  std::unordered_map<std::string, InodeEntry> table;
  table["file"].replicas = {"nodeA"};
  table["file"].partial = true;
  auto replicator = [&](const std::string &, const NodeID &, const NodeID &) {};
  RepairWorker worker(table, cache, 3, std::chrono::seconds(1), replicator);
  worker.start();
  auto start = SteadyClock::now();
  while (SteadyClock::now() - start < std::chrono::seconds(30)) {
    if (!table["file"].partial)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  worker.stop();
  REQUIRE_FALSE(table["file"].partial);
  REQUIRE(table["file"].replicas.size() >= 3);
}
