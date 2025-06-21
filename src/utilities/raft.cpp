#include "utilities/raft.h"
#include "utilities/metrics.h"
#include <random>
#include <sstream>

RaftNode::RaftNode(const std::string &id, const std::vector<std::string> &peers,
                   SendFunc func)
    : nodeId(id), peerIds(peers), sendFunc(std::move(func)),
      role(RaftRole::Follower), currentTerm(0), commitIndex(0), voteCount(0) {}

void RaftNode::start() {
  running = true;
  lastHeartbeat = std::chrono::steady_clock::now();
  electionThread = std::thread(&RaftNode::electionLoop, this);
}

void RaftNode::stop() {
  running = false;
  if (electionThread.joinable())
    electionThread.join();
  if (heartbeatThread.joinable())
    heartbeatThread.join();
}

bool RaftNode::isLeader() const {
  std::lock_guard<std::mutex> lk(mtx);
  return role == RaftRole::Leader;
}

std::string RaftNode::getLeader() const {
  std::lock_guard<std::mutex> lk(mtx);
  return currentLeader;
}

void RaftNode::sendMessage(const std::string &peer, const Message &m) {
  if (sendFunc) {
    sendFunc(peer, m);
    return;
  }
  try {
    std::string ip = peer.substr(0, peer.find(':'));
    int port = std::stoi(peer.substr(peer.find(':') + 1));
    Networking::Client client(ip.c_str(), port);
    client.Send(Message::Serialize(m).c_str());
    client.Disconnect();
  } catch (const std::exception &e) {
    Logger::getInstance().log(LogLevel::ERROR,
                              std::string("[RaftNode] sendMessage error to ") +
                                  peer + ": " + e.what());
  }
}

void RaftNode::resetElectionTimer() {
  lastHeartbeat = std::chrono::steady_clock::now();
}

void RaftNode::handleMessage(const Message &msg, const std::string &from) {
  Message resp;
  bool sendResp = false;

  std::vector<std::string> newCmds;
  {
    std::lock_guard<std::mutex> lk(mtx);
    if (msg._Type == MessageType::RaftAppendEntries) {
      int term = std::stoi(msg._Content);
      if (term >= currentTerm) {
        currentTerm = term;
        currentLeader = from;
        role = RaftRole::Follower;
        votedFor.clear();
        resetElectionTimer();
        if (!msg._Data.empty()) {
          log.clear();
          std::stringstream ss(msg._Data);
          std::string entry;
          while (std::getline(ss, entry, ';')) {
            if (entry.empty())
              continue;
            size_t pos = entry.find(':');
            if (pos == std::string::npos)
              continue;
            int t = std::stoi(entry.substr(0, pos));
            std::string cmd = entry.substr(pos + 1);
            log.push_back({t, cmd});
          }
        }
        if (static_cast<int>(log.size()) > commitIndex) {
          for (size_t i = commitIndex; i < log.size(); ++i) {
            newCmds.push_back(log[i].command);
          }
          commitIndex = log.size();
          MetricsRegistry::instance().setGauge(
              "simplidfs_raft_commit_index", static_cast<double>(commitIndex));
        }
      }
      resp._Type = MessageType::RaftAppendEntriesResponse;
      resp._NodeAddress = nodeId;
      resp._Content = std::to_string(currentTerm);
      sendResp = true;
    } else if (msg._Type == MessageType::RaftRequestVote) {
      int term = std::stoi(msg._Content);
      resp._Type = MessageType::RaftRequestVoteResponse;
      resp._NodeAddress = nodeId;
      if (term > currentTerm) {
        currentTerm = term;
        role = RaftRole::Follower;
        votedFor.clear();
      }
      bool grant = false;
      if (term == currentTerm && (votedFor.empty() || votedFor == from)) {
        grant = true;
        votedFor = from;
        resetElectionTimer();
      }
      resp._Content = std::to_string(currentTerm);
      resp._Data = grant ? "1" : "0";
      sendResp = true;
    } else if (msg._Type == MessageType::RaftRequestVoteResponse) {
      if (role == RaftRole::Candidate) {
        int term = std::stoi(msg._Content);
        if (term > currentTerm) {
          becomeFollower(term);
          return;
        }
        if (msg._Data == "1") {
          ++voteCount;
          if (voteCount > (static_cast<int>(peerIds.size()) + 1) / 2) {
            becomeLeader();
          }
        }
      }
    }
  }

  if (sendResp) {
    sendMessage(from, resp);
  }

  for (const auto &cmd : newCmds) {
    if (applyFunc)
      applyFunc(cmd);
  }
}

void RaftNode::startElection() {
  std::vector<std::pair<std::string, Message>> toSend;
  {
    std::lock_guard<std::mutex> lk(mtx);
    voteCount = 1;
    currentTerm++;
    votedFor = nodeId;
    for (const auto &p : peerIds) {
      Message req;
      req._Type = MessageType::RaftRequestVote;
      req._NodeAddress = nodeId;
      req._Content = std::to_string(currentTerm);
      toSend.emplace_back(p, req);
    }
  }
  for (const auto &pr : toSend) {
    sendMessage(pr.first, pr.second);
  }
}

void RaftNode::becomeLeader() {
  role = RaftRole::Leader;
  currentLeader = nodeId;
  MetricsRegistry::instance().setGauge("simplidfs_raft_role", 1,
                                       {{"role", "leader"}});
  Logger::getInstance().log(LogLevel::INFO, "[RaftNode] Node " + nodeId +
                                                " became leader for term " +
                                                std::to_string(currentTerm));
  if (heartbeatThread.joinable())
    heartbeatThread.join();
  heartbeatThread = std::thread(&RaftNode::heartbeatLoop, this);
}

void RaftNode::becomeFollower(int term) {
  role = RaftRole::Follower;
  currentLeader.clear();
  currentTerm = term;
  MetricsRegistry::instance().setGauge("simplidfs_raft_role", 1,
                                       {{"role", "follower"}});
  votedFor.clear();
  if (heartbeatThread.joinable())
    heartbeatThread.join();
}

void RaftNode::electionLoop() {
  std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(150, 300);
  int timeout = dist(rng);
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    bool start = false;
    {
      std::lock_guard<std::mutex> lk(mtx);
      if (role == RaftRole::Leader)
        continue;
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                lastHeartbeat)
              .count() > timeout) {
        role = RaftRole::Candidate;
        MetricsRegistry::instance().setGauge("simplidfs_raft_role", 1,
                                             {{"role", "candidate"}});
        start = true;
        timeout = dist(rng);
        lastHeartbeat = std::chrono::steady_clock::now();
      }
    }
    if (start) {
      startElection();
    }
  }
}

void RaftNode::heartbeatLoop() {
  while (running) {
    std::vector<std::pair<std::string, Message>> toSend;
    {
      std::lock_guard<std::mutex> lk(mtx);
      if (role != RaftRole::Leader)
        return;

      Message hb;
      hb._Type = MessageType::RaftAppendEntries;
      hb._NodeAddress = nodeId;
      hb._Content = std::to_string(currentTerm);
      std::stringstream ss;
      for (const auto &e : log) {
        ss << e.term << ':' << e.command << ';';
      }
      hb._Data = ss.str();

      for (const auto &p : peerIds) {
        toSend.emplace_back(p, hb);
      }
    }

    for (const auto &pr : toSend) {
      sendMessage(pr.first, pr.second);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void RaftNode::appendCommand(const std::string &command) {
  std::lock_guard<std::mutex> lk(mtx);
  if (role != RaftRole::Leader)
    return;
  log.push_back({currentTerm, command});
  commitIndex = log.size();
  MetricsRegistry::instance().setGauge("simplidfs_raft_commit_index",
                                       static_cast<double>(commitIndex));
  if (applyFunc)
    applyFunc(command);
}

std::vector<RaftLogEntry> RaftNode::getLog() const {
  std::lock_guard<std::mutex> lk(mtx);
  return log;
}
