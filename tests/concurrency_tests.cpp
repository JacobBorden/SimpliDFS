#include <gtest/gtest.h>
#include "metaserver/metaserver.h"
#include "utilities/server.h"
#include "utilities/client.h"
#include "utilities/message.h"
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>

/** Simple server accepting connections until stopped. */
struct DummyServer {
    Networking::Server server;
    std::thread th;
    std::atomic<bool> running{true};
    explicit DummyServer(int port) : server(port) {
        server.startListening();
        th = std::thread([this]() {
            while (running) {
                auto conn = server.Accept();
                if (!running) {
                    server.DisconnectClient(conn);
                    break;
                }
                (void)server.Receive(conn);
                server.Send("", conn);
                server.DisconnectClient(conn);
            }
        });
    }
    void stop() {
        running = false;
        try {
            Networking::Client c("127.0.0.1", server.GetPort());
            c.Send("");
            c.Disconnect();
        } catch (...) {}
        if (th.joinable()) th.join();
        server.Shutdown();
    }
};

/**
 * @brief Worker that repeatedly creates and deletes a file.
 * Each file is created with any currently registered nodes.
 */
static void worker_create_delete(MetadataManager& manager,
                                 std::vector<std::string>& registered,
                                 std::mutex& regMutex,
                                 int id) {
    for (int i = 0; i < 10; ++i) {
        std::string fname = "A_" + std::to_string(id) + "_" + std::to_string(i);
        std::vector<std::string> nodes;
        {
            std::lock_guard<std::mutex> lk(regMutex);
            nodes = registered;
        }
        manager.addFile(fname, nodes, 0644);
        manager.removeFile(fname);
    }
}

/**
 * @brief Worker that registers a node and adds a file hosted on that node.
 */
static void worker_register_and_add(MetadataManager& manager,
                                    std::vector<std::string>& registered,
                                    std::mutex& regMutex,
                                    int id) {
    std::string nodeId = "Node" + std::to_string(id);
    manager.registerNode(nodeId, "127.0.0.1", 12000 + id);
    {
        std::lock_guard<std::mutex> lk(regMutex);
        registered.push_back(nodeId);
    }
    manager.addFile("B_" + std::to_string(id), {nodeId}, 0644);
}

/**
 * @brief Worker that periodically sends heartbeats for a node.
 */
static void worker_heartbeat(MetadataManager& manager,
                             const std::string& nodeId) {
    for (int i = 0; i < 20; ++i) {
        manager.processHeartbeat(nodeId);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

/**
 * @brief Worker that repeatedly checks for dead nodes.
 */
static void worker_deadcheck(MetadataManager& manager) {
    for (int i = 0; i < 20; ++i) {
        manager.checkForDeadNodes();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

TEST(MetadataConcurrency, ConcurrentOps) {
    MetadataManager manager;
    DummyServer s0(12000), s1(12001);
    std::vector<std::string> registered;
    std::mutex regMutex;

    const int creatorThreads = 4;
    const int registerThreads = 2;

    std::vector<std::thread> creators;
    std::vector<std::thread> registrars;

    for (int i = 0; i < registerThreads; ++i) {
        registrars.emplace_back(worker_register_and_add,
                                std::ref(manager),
                                std::ref(registered),
                                std::ref(regMutex),
                                i);
    }

    for (int i = 0; i < creatorThreads; ++i) {
        creators.emplace_back(worker_create_delete,
                              std::ref(manager),
                              std::ref(registered),
                              std::ref(regMutex),
                              i);
    }

    for (auto& t : registrars) {
        t.join();
    }

    std::vector<std::thread> heartbeats;
    for (const auto& id : registered) {
        heartbeats.emplace_back(worker_heartbeat, std::ref(manager), id);
    }

    std::thread checker(worker_deadcheck, std::ref(manager));

    for (auto& t : creators) {
        t.join();
    }
    for (auto& t : heartbeats) {
        t.join();
    }
    if (checker.joinable()) checker.join();

    s0.stop();
    s1.stop();

    // Validate nodes remain registered
    for (int i = 0; i < registerThreads; ++i) {
        EXPECT_TRUE(manager.isNodeRegistered("Node" + std::to_string(i)));
    }

    // All files created by workers should be removed
    EXPECT_TRUE(manager.getAllFileNames().empty());
}

