#pragma once
#include "utilities/client.h"
#include "utilities/logger.h"
#include "utilities/message.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class RaftRole { Follower, Candidate, Leader };

struct RaftLogEntry {
  int term;
  std::string command;
};

class RaftNode {
public:
  using SendFunc = std::function<void(const std::string &, const Message &)>;
  using ApplyFunc = std::function<void(const std::vector<RaftLogEntry> &)>;

  RaftNode(const std::string &id, const std::vector<std::string> &peers,
           SendFunc func = nullptr);

  void start();
  void stop();

  bool isLeader() const;
  std::string getLeader() const;

  /**
   * @brief Retrieve the current log entries for testing.
   *
   * @return A copy of the log vector.
   */
  std::vector<RaftLogEntry> getLog() const;

  void handleMessage(const Message &msg, const std::string &from);
  void appendCommand(const std::string &command);
  void setApplyCallback(ApplyFunc cb) { applyCb = std::move(cb); }

private:
  void electionLoop();
  void heartbeatLoop();
  void resetElectionTimer();
  void startElection();
  void becomeFollower(int term);
  void becomeLeader();
  void sendMessage(const std::string &peer, const Message &m);

  std::string nodeId;
  std::vector<std::string> peerIds;
  SendFunc sendFunc;

  mutable std::mutex mtx;
  RaftRole role;
  int currentTerm;
  std::string votedFor;
  std::string currentLeader;
  std::vector<RaftLogEntry> log;
  int commitIndex;
  int voteCount;

  ApplyFunc applyCb;

  std::atomic<bool> running{false};
  std::thread electionThread;
  std::thread heartbeatThread;
  std::chrono::steady_clock::time_point lastHeartbeat;
};
