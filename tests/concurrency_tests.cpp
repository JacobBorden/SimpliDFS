#include "metaserver/metaserver.h"
#include "utilities/client.h"
#include "utilities/message.h"
#include "utilities/server.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

static int getEphemeralPort() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  socklen_t len = sizeof(addr);
  getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &len);
  int port = ntohs(addr.sin_port);
  close(sock);
  return port;
}

static int registerPorts[2];

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
        } catch (...) {
        }
        server.Shutdown();
        if (th.joinable())
          th.join();
    }
    int port() { return server.GetPort(); }
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
    manager.registerNode(nodeId, "127.0.0.1", registerPorts[id]);
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
    registerPorts[0] = getEphemeralPort();
    registerPorts[1] = getEphemeralPort();
    DummyServer s0(registerPorts[0]), s1(registerPorts[1]);
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

