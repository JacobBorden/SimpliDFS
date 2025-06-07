#include "gtest/gtest.h"
#include "utilities/raft.h"
#include <unordered_map>
#include <random>
#include <thread>
#include <chrono>

class InMemoryNetworkChaos {
public:
    std::unordered_map<std::string, RaftNode*> nodes;
    void send(const std::string& from, const std::string& to, const Message& m) {
        auto it = nodes.find(to);
        if (it != nodes.end()) {
            Message copy = m;
            copy._NodeAddress = from;
            it->second->handleMessage(copy, from);
        }
    }
};

TEST(RaftChaos, KillLeaderNoDataLoss) {
    InMemoryNetworkChaos net;
    std::vector<std::string> ids = {"A","B","C"};
    std::unordered_map<std::string, std::unique_ptr<RaftNode>> nodes;
    for (const auto& id : ids) {
        std::vector<std::string> peers;
        for (const auto& other : ids) if (other != id) peers.push_back(other);
        nodes[id] = std::make_unique<RaftNode>(id, peers,
            [&](const std::string& peer, const Message& m){ net.send(id, peer, m); });
        net.nodes[id] = nodes[id].get();
        nodes[id]->start();
    }

    // Wait for leader election
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::string leader;
    for (const auto& id : ids) {
        if (nodes[id]->isLeader()) {
            leader = id;
            break;
        }
    }
    ASSERT_FALSE(leader.empty());

    nodes[leader]->appendCommand("cmd1");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Kill the leader
    nodes[leader]->stop();
    net.nodes.erase(leader);

    // Wait for re-election
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::string newLeader;
    int leaderCount = 0;
    for (const auto& id : ids) {
        if (id == leader) continue;
        if (nodes[id]->isLeader()) {
            newLeader = id;
            leaderCount++;
        }
    }
    EXPECT_EQ(leaderCount, 1);
    ASSERT_FALSE(newLeader.empty());

    nodes[newLeader]->appendCommand("cmd2");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Restart old leader to verify log replication
    nodes[leader]->start();
    net.nodes[leader] = nodes[leader].get();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto expected = nodes[newLeader]->getLog();
    for (const auto& id : ids) {
        auto log = nodes[id]->getLog();
        ASSERT_EQ(log.size(), expected.size());
        for (size_t i = 0; i < log.size(); ++i) {
            EXPECT_EQ(log[i].command, expected[i].command);
        }
    }

    for (auto& p : nodes) p.second->stop();
}

