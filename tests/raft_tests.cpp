#include "gtest/gtest.h"
#include "utilities/raft.h"

class InMemoryNetwork {
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

TEST(RaftBasic, LeaderElection) {
    InMemoryNetwork net;
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
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int leaders = 0;
    for (const auto& id : ids) {
        if (nodes[id]->isLeader()) leaders++;
    }
    for (auto& p : nodes) p.second->stop();
    EXPECT_EQ(leaders, 1);
}

