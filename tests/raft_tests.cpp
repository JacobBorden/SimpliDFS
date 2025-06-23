#include "gtest/gtest.h"
#include "utilities/raft.h"

class InMemoryNetwork {
public:
  std::unordered_map<std::string, RaftNode *> nodes;
  void send(const std::string &from, const std::string &to, const Message &m) {
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
  std::vector<std::string> ids = {"A", "B", "C"};
  std::unordered_map<std::string, std::unique_ptr<RaftNode>> nodes;
  for (const auto &id : ids) {
    std::vector<std::string> peers;
    for (const auto &other : ids)
      if (other != id)
        peers.push_back(other);
    nodes[id] = std::make_unique<RaftNode>(
        id, peers, [&](const std::string &peer, const Message &m) {
          net.send(id, peer, m);
        });
    net.nodes[id] = nodes[id].get();
    nodes[id]->start();
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  int leaders = 0;
  for (const auto &id : ids) {
    if (nodes[id]->isLeader())
      leaders++;
  }
  for (auto &p : nodes)
    p.second->stop();
  EXPECT_EQ(leaders, 1);
}

TEST(RaftSnapshot, Restoration) {
  InMemoryNetwork net;
  std::vector<std::string> ids = {"A", "B"};
  std::unordered_map<std::string, std::unique_ptr<RaftNode>> nodes;
  for (const auto &id : ids) {
    std::vector<std::string> peers;
    for (const auto &other : ids)
      if (other != id)
        peers.push_back(other);
    nodes[id] = std::make_unique<RaftNode>(
        id, peers, [&](const std::string &peer, const Message &m) {
          net.send(id, peer, m);
        });
    net.nodes[id] = nodes[id].get();
    nodes[id]->start();
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::string leader;
  for (const auto &id : ids) {
    if (nodes[id]->isLeader())
      leader = id;
  }
  ASSERT_FALSE(leader.empty());
  std::string follower = leader == ids[0] ? ids[1] : ids[0];
  auto &ln = nodes[leader];
  auto &fn = nodes[follower];

  ln->appendCommand("cmd1");
  ln->appendCommand("cmd2");
  ln->sendSnapshot(follower);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  auto flog = fn->getLog();
  for (auto &p : nodes)
    p.second->stop();
  ASSERT_EQ(flog.size(), 2U);
  EXPECT_EQ(flog[0].command, "cmd1");
  EXPECT_EQ(flog[1].command, "cmd2");
}

TEST(RaftLogCompaction, Trim) {
  InMemoryNetwork net;
  std::vector<std::string> ids = {"A", "B"};
  std::unordered_map<std::string, std::unique_ptr<RaftNode>> nodes;
  for (const auto &id : ids) {
    std::vector<std::string> peers;
    for (const auto &o : ids)
      if (o != id)
        peers.push_back(o);
    nodes[id] = std::make_unique<RaftNode>(
        id, peers, [&](const std::string &peer, const Message &m) {
          net.send(id, peer, m);
        });
    net.nodes[id] = nodes[id].get();
    nodes[id]->start();
  }
  std::this_thread::sleep_for(std::chrono::seconds(1));
  std::string leader;
  for (const auto &id : ids)
    if (nodes[id]->isLeader())
      leader = id;
  ASSERT_FALSE(leader.empty());
  auto &ln = nodes[leader];
  for (int i = 0; i < 3; ++i)
    ln->appendCommand("c" + std::to_string(i));
  ln->compactLog(1);
  auto log = ln->getLog();
  for (auto &p : nodes)
    p.second->stop();
  ASSERT_EQ(log.size(), 1U);
  EXPECT_EQ(log[0].command, "c2");
}
