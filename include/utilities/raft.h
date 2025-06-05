#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include "utilities/message.h"
#include "utilities/client.h"
#include "utilities/logger.h"

enum class RaftRole {Follower, Candidate, Leader};

struct RaftLogEntry {
    int term;
    std::string command;
};

class RaftNode {
public:
    using SendFunc = std::function<void(const std::string&, const Message&)>;

    RaftNode(const std::string& id,
             const std::vector<std::string>& peers,
             SendFunc func = nullptr);

    void start();
    void stop();

    bool isLeader() const;
    std::string getLeader() const;

    void handleMessage(const Message& msg, const std::string& from);
    void appendCommand(const std::string& command);

private:
    void electionLoop();
    void heartbeatLoop();
    void resetElectionTimer();
    void startElection();
    void becomeFollower(int term);
    void becomeLeader();
    void sendMessage(const std::string& peer, const Message& m);

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

    std::atomic<bool> running{false};
    std::thread electionThread;
    std::thread heartbeatThread;
    std::chrono::steady_clock::time_point lastHeartbeat;
};
