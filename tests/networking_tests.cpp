#include "gtest/gtest.h"
#include "utilities/client.h"
#include "utilities/server.h"
#include "utilities/networkexception.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include "utilities/logger.h" // Add this include
#include <cstdio>   // For std::remove
#include <string>   // For std::to_string in TearDown
#include "metaserver/metaserver.h" // For MetadataManager
#include "node/node.h"             // For Node
#include "utilities/message.h"     // For Message, MessageType

// Basic test fixture for networking tests if needed, or just use TEST directly.
class NetworkingTest : public ::testing::Test {
protected:
    // Optional: Set up resources shared by tests in this fixture
    void SetUp() override {
        try {
            Logger::init("networking_tests.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            // Handle or log if SetUp itself fails critically
        }
    }

    // Optional: Clean up shared resources
    void TearDown() override {
        try {
            Logger::init("dummy_net_cleanup.log", LogLevel::DEBUG);
            std::remove("dummy_net_cleanup.log"); 
            std::remove("dummy_net_cleanup.log.1"); // Clean up potential rotated dummy log
        } catch (const std::runtime_error& e) { /* ignore */ }
        
        std::remove("networking_tests.log");
        for (int i = 1; i <= 5; ++i) {
            std::remove(("networking_tests.log." + std::to_string(i)).c_str());
        }
    }
};

TEST_F(NetworkingTest, ServerInitialization) { // Changed to TEST_F
    Networking::Server server(12345);
    ASSERT_TRUE(server.InitServer()); // Assuming InitServer is the main initialization step
    ASSERT_TRUE(server.ServerIsRunning()); // Or a similar check
    ASSERT_EQ(server.GetPort(), 12345);
    server.Shutdown(); // Ensure server is properly closed
}

TEST_F(NetworkingTest, ClientInitializationAndConnection) { // Changed to TEST_F
    Networking::Server server(12346);
    ASSERT_TRUE(server.InitServer());
    std::thread serverThread([&]() {
        server.Accept(); // Accept one connection
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Increased delay to ensure server is listening

    Networking::Client client("127.0.0.1", 12346);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) { // More retries
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client = Networking::Client("127.0.0.1", 12346);
        retries++;
    }
    ASSERT_TRUE(client.IsConnected());
    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());
    if (serverThread.joinable()) serverThread.join();
    server.Shutdown();
}

TEST_F(NetworkingTest, FuseMkdirSimulation) {
    const int testPort = 12391;
    std::map<std::string, uint32_t> mockDirectories; // path -> mode

    const int METASERVER_EEXIST = 17; // Standard EEXIST
    const int METASERVER_ENOENT = 2;  // Standard ENOENT

    auto get_parent_path = [](const std::string& path) -> std::string {
        if (path.empty() || path == "/") return ""; // No parent for root or empty
        size_t last_slash = path.find_last_of('/');
        if (last_slash == 0) return "/"; // Parent is root, e.g., for "/foo"
        if (last_slash == std::string::npos) return ""; // Should not happen for valid absolute paths
        return path.substr(0, last_slash);
    };

    auto run_server_for_mkdir_scenario = 
        [&](std::map<std::string, uint32_t>& currentMockDirs) -> Networking::Server* {
        Networking::Server* server = new Networking::Server(testPort);
        EXPECT_TRUE(server->InitServer());

        std::thread([server, &currentMockDirs, get_parent_path]() {
            try {
                Networking::ClientConnection conn = server->Accept();
                EXPECT_TRUE(conn.clientSocket != 0);

                std::vector<char> rawReq = server->Receive(conn);
                EXPECT_FALSE(rawReq.empty());
                Message reqMsg = Message::Deserialize(std::string(rawReq.begin(), rawReq.end()));
                EXPECT_EQ(reqMsg._Type, MessageType::Mkdir);

                Message respMsg;
                respMsg._Type = MessageType::MkdirResponse;
                respMsg._Path = reqMsg._Path;
                respMsg._ErrorCode = 0;

                std::string parentPath = get_parent_path(reqMsg._Path);
                
                if (currentMockDirs.count(reqMsg._Path)) {
                    respMsg._ErrorCode = METASERVER_EEXIST;
                } else if (!parentPath.empty() && parentPath != "/" && !currentMockDirs.count(parentPath)) {
                    // Parent path doesn't exist, and it's not a root-level directory creation
                    respMsg._ErrorCode = METASERVER_ENOENT;
                } else if (parentPath.empty() && reqMsg._Path != "/") { // Trying to create e.g. "foo" not "/foo"
                     respMsg._ErrorCode = METASERVER_ENOENT; // Invalid path if not absolute
                }
                else {
                    currentMockDirs[reqMsg._Path] = reqMsg._Mode;
                    Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Created " + reqMsg._Path + " with mode " + std::to_string(reqMsg._Mode));
                }
                
                server->Send(Message::Serialize(respMsg).c_str(), conn);
                server->DisconnectClient(conn);
            } catch (const Networking::NetworkException& e) {
                ADD_FAILURE() << "Server thread NetworkException: " << e.what();
            } catch (const std::exception& e) {
                ADD_FAILURE() << "Server thread std::exception: " << e.what();
            }
        }).detach();
        return server;
    };

    auto run_client_for_mkdir_scenario = 
        [&](const std::string& path, uint32_t mode, int expectedErrorCode) {
        Networking::Client client("127.0.0.1", testPort);
        EXPECT_TRUE(client.IsConnected());

        Message mkdirReq;
        mkdirReq._Type = MessageType::Mkdir;
        mkdirReq._Path = path;
        mkdirReq._Mode = mode;

        client.Send(Message::Serialize(mkdirReq).c_str());
        
        std::vector<char> rawResp = client.Receive();
        EXPECT_FALSE(rawResp.empty());
        Message mkdirResp = Message::Deserialize(std::string(rawResp.begin(), rawResp.end()));

        EXPECT_EQ(mkdirResp._Type, MessageType::MkdirResponse);
        EXPECT_EQ(mkdirResp._Path, path);
        EXPECT_EQ(mkdirResp._ErrorCode, expectedErrorCode);
        
        client.Disconnect();
    };
    
    Networking::Server* currentServer = nullptr;
    const uint32_t testMode = 0755; // S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH

    // Scenario 1: Successful creation of a root-level directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 1 - Successful create /testdir");
        mockDirectories.clear();
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/testdir", testMode, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/testdir"));
        if (mockDirectories.count("/testdir")) {
            ASSERT_EQ(mockDirectories["/testdir"], testMode);
        }
    }

    // Scenario 2: Attempt to create existing directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 2 - Create existing /testdir");
        // mockDirectories already has "/testdir" from Scenario 1
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/testdir", testMode, METASERVER_EEXIST);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/testdir")); // Should still be there
    }

    // Scenario 3: Successful creation of a nested directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 3 - Successful create /testdir/subdir");
        // mockDirectories has "/testdir"
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/testdir/subdir", testMode, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/testdir/subdir"));
        if (mockDirectories.count("/testdir/subdir")) {
            ASSERT_EQ(mockDirectories["/testdir/subdir"], testMode);
        }
    }
    
    // Scenario 4: Attempt to create in a non-existent parent path
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 4 - Create /nonexistentparent/newdir");
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/nonexistentparent/newdir", testMode, METASERVER_ENOENT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_FALSE(mockDirectories.count("/nonexistentparent/newdir"));
    }
    
    // Scenario 5: Attempt to create a directory that is not absolute (e.g. "relative_dir")
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 5 - Create relative_dir");
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("relative_dir", testMode, METASERVER_ENOENT); // Or a different error for invalid path format
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_FALSE(mockDirectories.count("relative_dir"));
    }
}

TEST_F(NetworkingTest, ServerShutdownWithActiveClients) {
    const int testPort = 12389;
    const int numClients = 3;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<int> clientsSuccessfullyConnected{0};
    std::atomic<int> serverHandlersStarted{0};
    std::atomic<int> serverHandlersUnblocked{0};
    std::atomic<int> clientsUnblocked{0};
    std::atomic<bool> serverShutdownCalled{false};

    std::vector<std::thread> serverWorkerThreads;
    std::thread serverAcceptLoopThread([&]() {
        try {
            for (int i = 0; i < numClients; ++i) {
                if (serverShutdownCalled.load()) break; // Stop trying to accept if shutdown initiated
                
                Logger::getInstance().log(LogLevel::DEBUG, "S_ACCEPT: Waiting for client " + std::to_string(i));
                Networking::ClientConnection conn = server.Accept();
                if (conn.clientSocket == 0) {
                    if (serverShutdownCalled.load()) {
                        Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept returned invalid socket during shutdown.");
                        break;
                    }
                    FAIL() << "S_ACCEPT: Accept failed for client " << i << " unexpectedly.";
                    break; 
                }
                Logger::getInstance().log(LogLevel::DEBUG, "S_ACCEPT: Accepted client " + std::to_string(i));
                
                serverWorkerThreads.emplace_back([&server, conn, i, &serverHandlersStarted, &serverHandlersUnblocked, &serverShutdownCalled]() {
                    std::string logPrefix = "S_WORKER_C" + std::to_string(i);
                    serverHandlersStarted++;
                    Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Started, attempting blocking Receive.");
                    try {
                        std::vector<char> data = server.Receive(conn);
                        // Expect Receive to unblock due to shutdown, returning empty or throwing.
                        if (serverShutdownCalled.load()) {
                            ASSERT_TRUE(data.empty()) << logPrefix << ": Receive unblocked but returned data during shutdown.";
                            Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Receive unblocked as expected (empty data).");
                        } else {
                            // This case should not happen if client doesn't send anything before shutdown.
                            Logger::getInstance().log(LogLevel::WARNING, logPrefix + ": Receive unblocked unexpectedly with data size: " + std::to_string(data.size()));
                        }
                    } catch (const Networking::NetworkException& e) {
                        Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Receive caught NetworkException as expected: " + std::string(e.what()));
                    } catch (const std::exception& e) {
                        FAIL() << logPrefix << ": Receive caught unexpected std::exception: " << e.what();
                    }
                    serverHandlersUnblocked++;
                    Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Unblocked and completed.");
                    // Server worker might try to DisconnectClient, which could also throw if socket is already closed by shutdown
                    try {
                        server.DisconnectClient(conn);
                    } catch (const Networking::NetworkException& e) {
                         Logger::getInstance().log(LogLevel::INFO, logPrefix + ": DisconnectClient caught NetworkException (potentially expected): " + std::string(e.what()));
                    }
                });
            }
        } catch (const Networking::NetworkException& e) {
            if (serverShutdownCalled.load()) {
                Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept loop caught NetworkException during/after shutdown (expected): " + std::string(e.what()));
            } else {
                FAIL() << "S_ACCEPT: NetworkException in Accept loop: " << e.what();
            }
        }
        Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept loop finished.");
    });

    std::vector<std::thread> clientThreads;
    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([i, testPort, &clientsSuccessfullyConnected, &clientsUnblocked, &serverShutdownCalled]() {
            std::string logPrefix = "CLIENT_C" + std::to_string(i);
            Networking::Client client("127.0.0.1", testPort);
            int retries = 0;
            while(!client.IsConnected() && retries < 10 && !serverShutdownCalled.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt
                retries++;
            }

            if (!client.IsConnected()) {
                 // If server shutdown happened before client could connect, this is not a test failure for this client.
                if(serverShutdownCalled.load()){
                    Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Could not connect, server already shutting down.");
                    clientsUnblocked++; // Count as "processed" for joining purposes
                    return;
                }
                FAIL() << logPrefix << ": Failed to connect.";
                clientsUnblocked++;
                return;
            }
            ASSERT_TRUE(client.IsConnected()) << logPrefix << ": Failed to connect.";
            clientsSuccessfullyConnected++;
            Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Connected. Attempting blocking Receive.");

            try {
                std::vector<char> data = client.Receive(); // Should block then unblock on server shutdown
                ASSERT_TRUE(data.empty()) << logPrefix << ": Client Receive unblocked but returned data.";
                Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Client Receive unblocked as expected (empty data).");
            } catch (const Networking::NetworkException& e) {
                Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Client Receive caught NetworkException as expected: " + std::string(e.what()));
            } catch (const std::exception& e) {
                FAIL() << logPrefix << ": Client Receive caught unexpected std::exception: " << e.what();
            }

            ASSERT_FALSE(client.IsConnected()) << logPrefix << ": IsConnected should be false after server shutdown unblocked Receive.";

            bool sendFailedAsExpected = false;
            try {
                client.Send("post-shutdown"); // Should fail
            } catch (const Networking::NetworkException& e) {
                sendFailedAsExpected = true;
                Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Client Send failed as expected after shutdown: " + std::string(e.what()));
            }
            ASSERT_TRUE(sendFailedAsExpected) << logPrefix << ": Client Send did not fail as expected after shutdown.";
            ASSERT_FALSE(client.IsConnected()) << logPrefix << ": IsConnected should remain false.";
            
            clientsUnblocked++;
            Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Completed.");
        });
    }

    // Wait for all clients to connect and server handlers to start their blocking receive
    int wait_retries = 0;
    while((clientsSuccessfullyConnected.load() < numClients || serverHandlersStarted.load() < numClients) && wait_retries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_retries++;
        if(serverShutdownCalled.load()) break; // Abort wait if shutdown already (e.g. server failed to init)
    }
    if(clientsSuccessfullyConnected.load() < numClients && !serverShutdownCalled.load()){
         Logger::getInstance().log(LogLevel::WARNING, "Timeout or issue: Not all clients connected successfully before shutdown. Connected: " + std::to_string(clientsSuccessfullyConnected.load()));
    }
     if(serverHandlersStarted.load() < numClients && !serverShutdownCalled.load()){
         Logger::getInstance().log(LogLevel::WARNING, "Timeout or issue: Not all server handlers started before shutdown. Started: " + std::to_string(serverHandlersStarted.load()));
    }
    // Even if not all clients connect (e.g. if server accept loop is slow or has issues), proceed to test shutdown.

    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Calling server.Shutdown().");
    serverShutdownCalled = true; // Signal all threads that shutdown is in progress
    server.Shutdown(); // This is the main call being tested for its behavior
    ASSERT_FALSE(server.ServerIsRunning()) << "ServerIsRunning should be false after Shutdown.";
    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: server.Shutdown() completed.");

    // Join server accept thread first
    if (serverAcceptLoopThread.joinable()) {
        serverAcceptLoopThread.join();
    }
     Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Server accept loop thread joined.");

    // Join server worker threads
    for (auto& t : serverWorkerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ASSERT_EQ(serverHandlersUnblocked.load(), clientsSuccessfullyConnected.load()) << "Not all server handlers unblocked or an incorrect number of handlers were started.";
    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Server worker threads joined.");

    // Join client threads
    for (auto& t : clientThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ASSERT_EQ(clientsUnblocked.load(), numClients) << "Not all clients unblocked or completed.";
    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Client threads joined.");
}

TEST_F(NetworkingTest, ServerShutdownWhileBlockedInAccept) {
    const int testPort = 12388;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<bool> serverAttemptingAccept{false};
    std::atomic<bool> acceptCallUnblocked{false};
    std::atomic<bool> serverShutdownInProgress{false};
    std::atomic<bool> serverThreadCorrectlyHandledUnblock{false};

    std::thread serverAcceptBlockThread([&]() {
        Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Thread started.");
        serverAttemptingAccept = true;
        try {
            Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Calling Accept().");
            Networking::ClientConnection conn = server.Accept();
            acceptCallUnblocked = true; // Accept returned

            if (serverShutdownInProgress.load()) {
                // If shutdown was called, Accept should ideally return an invalid socket or throw.
                // A valid socket here means Accept didn't get interrupted correctly or a late client connected.
                ASSERT_EQ(conn.clientSocket, 0) << "S_AcceptBlock: Accept unblocked during shutdown but returned a valid socket.";
                if (conn.clientSocket == 0) {
                    serverThreadCorrectlyHandledUnblock = true;
                    Logger::getInstance().log(LogLevel::INFO, "S_AcceptBlock: Accept unblocked as expected during shutdown, returning invalid socket.");
                }
            } else {
                // This case should not be reached if no client connects.
                // If a client somehow connected, log it. This is unexpected for this test.
                if (conn.clientSocket != 0) {
                     Logger::getInstance().log(LogLevel::WARNING, "S_AcceptBlock: Accept unblocked and returned a valid client socket unexpectedly.");
                     server.DisconnectClient(conn); // Clean up if it happened
                } else {
                    // Accept returned invalid socket without shutdown_in_progress: an error.
                     Logger::getInstance().log(LogLevel::ERROR, "S_AcceptBlock: Accept returned invalid socket without shutdown signal.");
                }
                FAIL() << "S_AcceptBlock: Accept unblocked unexpectedly without server shutdown signal or with a valid client.";
            }
        } catch (const Networking::NetworkException& e) {
            acceptCallUnblocked = true; // Exception means it also unblocked
            if (serverShutdownInProgress.load()) {
                serverThreadCorrectlyHandledUnblock = true;
                Logger::getInstance().log(LogLevel::INFO, "S_AcceptBlock: Accept caught NetworkException as expected during shutdown: " + std::string(e.what()));
            } else {
                FAIL() << "S_AcceptBlock: Accept caught NetworkException unexpectedly: " << e.what();
            }
        } catch (const std::exception& e) {
            acceptCallUnblocked = true;
            FAIL() << "S_AcceptBlock: Accept caught unexpected std::exception: " << e.what();
        }
        Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Thread finished.");
    });

    // Wait for the server thread to be in the Accept call
    int retries = 0;
    while (!serverAttemptingAccept.load() && retries < 50) { // Max 5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retries++;
    }
    ASSERT_TRUE(serverAttemptingAccept.load()) << "Server thread did not signal that it's attempting to accept.";
    // Add a very small delay to increase likelihood of Accept() being called and blocked.
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); 

    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Setting shutdown flag and calling server.Shutdown().");
    serverShutdownInProgress = true; // Signal that shutdown is intentional
    server.Shutdown(); // This should cause Accept() to unblock

    ASSERT_FALSE(server.ServerIsRunning()) << "ServerIsRunning should be false after Shutdown call.";
    Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: server.Shutdown() completed.");

    if (serverAcceptBlockThread.joinable()) {
        serverAcceptBlockThread.join();
    }
    
    ASSERT_TRUE(acceptCallUnblocked.load()) << "Accept call did not unblock after server shutdown.";
    ASSERT_TRUE(serverThreadCorrectlyHandledUnblock.load()) << "Server thread did not correctly handle the unblocking of Accept.";
}

TEST_F(NetworkingTest, FuseRmdirSimulation) {
    const int testPort = 12390;
    std::map<std::string, uint32_t> mockDirectories; // path -> mode

    const int METASERVER_ENOENT = 2;    // Standard ENOENT
    const int METASERVER_ENOTEMPTY = 39; // Standard ENOTEMPTY
    const int METASERVER_EBUSY = 16;     // Standard EBUSY (for removing root or busy resource)
    // const int METASERVER_EPERM = 1;   // Standard EPERM (alternative for root)

    auto is_directory_non_empty = 
        [&](const std::map<std::string, uint32_t>& currentMockDirs, const std::string& dirPath) -> bool {
        std::string prefix = dirPath;
        if (prefix.back() != '/') {
            prefix += '/';
        }
        // Root directory "/" check needs to be handled carefully if it's part of mockDirectories explicitly
        // For this check, if dirPath is "/", any other entry means root is not empty.
        if (dirPath == "/") {
            return currentMockDirs.size() > (currentMockDirs.count("/") ? 1u : 0u);
        }

        for (const auto& entry : currentMockDirs) {
            if (entry.first != dirPath && entry.first.rfind(prefix, 0) == 0) { // entry.first starts with prefix
                return true; // Found a child
            }
        }
        return false;
    };

    auto run_server_for_rmdir_scenario = 
        [&](std::map<std::string, uint32_t>& currentMockDirs) -> Networking::Server* {
        Networking::Server* server = new Networking::Server(testPort);
        EXPECT_TRUE(server->InitServer());

        std::thread([server, &currentMockDirs, is_directory_non_empty]() {
            try {
                Networking::ClientConnection conn = server->Accept();
                EXPECT_TRUE(conn.clientSocket != 0);

                std::vector<char> rawReq = server->Receive(conn);
                EXPECT_FALSE(rawReq.empty());
                Message reqMsg = Message::Deserialize(std::string(rawReq.begin(), rawReq.end()));
                EXPECT_EQ(reqMsg._Type, MessageType::Rmdir);

                Message respMsg;
                respMsg._Type = MessageType::RmdirResponse;
                respMsg._Path = reqMsg._Path;
                respMsg._ErrorCode = 0;

                if (reqMsg._Path == "/") {
                    respMsg._ErrorCode = METASERVER_EBUSY; // Cannot remove root
                } else if (!currentMockDirs.count(reqMsg._Path)) {
                    respMsg._ErrorCode = METASERVER_ENOENT; // Directory does not exist
                } else if (is_directory_non_empty(currentMockDirs, reqMsg._Path)) {
                    respMsg._ErrorCode = METASERVER_ENOTEMPTY; // Directory not empty
                } else {
                    currentMockDirs.erase(reqMsg._Path); // Success
                    Logger::getInstance().log(LogLevel::DEBUG, "S_RMDIR: Removed " + reqMsg._Path);
                }
                
                server->Send(Message::Serialize(respMsg).c_str(), conn);
                server->DisconnectClient(conn);
            } catch (const Networking::NetworkException& e) {
                ADD_FAILURE() << "Server thread NetworkException: " << e.what();
            } catch (const std::exception& e) {
                ADD_FAILURE() << "Server thread std::exception: " << e.what();
            }
        }).detach();
        return server;
    };

    auto run_client_for_rmdir_scenario = 
        [&](const std::string& path, int expectedErrorCode) {
        Networking::Client client("127.0.0.1", testPort);
        EXPECT_TRUE(client.IsConnected());

        Message rmdirReq;
        rmdirReq._Type = MessageType::Rmdir;
        rmdirReq._Path = path;

        client.Send(Message::Serialize(rmdirReq).c_str());
        
        std::vector<char> rawResp = client.Receive();
        EXPECT_FALSE(rawResp.empty());
        Message rmdirResp = Message::Deserialize(std::string(rawResp.begin(), rawResp.end()));

        EXPECT_EQ(rmdirResp._Type, MessageType::RmdirResponse);
        EXPECT_EQ(rmdirResp._Path, path);
        EXPECT_EQ(rmdirResp._ErrorCode, expectedErrorCode);
        
        client.Disconnect();
    };
    
    Networking::Server* currentServer = nullptr;
    const uint32_t testMode = 0755;

    // Scenario 1: Successful removal of an existing empty directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_RMDIR: Scenario 1 - Successful remove /dir_to_remove");
        mockDirectories.clear();
        mockDirectories["/dir_to_remove"] = testMode;
        currentServer = run_server_for_rmdir_scenario(mockDirectories);
        run_client_for_rmdir_scenario("/dir_to_remove", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        currentServer->Shutdown(); delete currentServer;
        ASSERT_FALSE(mockDirectories.count("/dir_to_remove"));
    }

    // Scenario 2: Attempt to remove a non-existent directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_RMDIR: Scenario 2 - Remove /non_existent_dir");
        mockDirectories.clear(); // Ensure it's not there
        currentServer = run_server_for_rmdir_scenario(mockDirectories);
        run_client_for_rmdir_scenario("/non_existent_dir", METASERVER_ENOENT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
    }

    // Scenario 3: Attempt to remove a non-empty directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_RMDIR: Scenario 3 - Remove non-empty /parent");
        mockDirectories.clear();
        mockDirectories["/parent"] = testMode;
        mockDirectories["/parent/child"] = testMode;
        currentServer = run_server_for_rmdir_scenario(mockDirectories);
        run_client_for_rmdir_scenario("/parent", METASERVER_ENOTEMPTY);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/parent")); // Should still be there
        ASSERT_TRUE(mockDirectories.count("/parent/child")); // Child should also be there
    }
    
    // Scenario 4: Attempt to remove the root directory "/"
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_RMDIR: Scenario 4 - Remove /");
        mockDirectories.clear();
        mockDirectories["/"] = testMode; // Some systems might not explicitly store root like this
                                         // but our check `reqMsg._Path == "/"` handles it.
        mockDirectories["/somefile"] = testMode; // Make root non-empty for a more robust check
        currentServer = run_server_for_rmdir_scenario(mockDirectories);
        run_client_for_rmdir_scenario("/", METASERVER_EBUSY); // Or EPERM based on implementation
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        // Root should not be removed. If it was explicitly added, it might still be in mockDirectories.
        // The key is the error code.
    }

    // Scenario 5: Successful removal of an empty nested directory
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_RMDIR: Scenario 5 - Successful remove /parent/empty_child");
        mockDirectories.clear();
        mockDirectories["/parent"] = testMode;
        mockDirectories["/parent/empty_child"] = testMode;
        currentServer = run_server_for_rmdir_scenario(mockDirectories);
        run_client_for_rmdir_scenario("/parent/empty_child", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/parent"));
        ASSERT_FALSE(mockDirectories.count("/parent/empty_child"));
    }
}

TEST_F(NetworkingTest, FuseWriteSimulation) {
    const int testPort = 12392;
    // Use a fresh Server instance for each scenario to simplify state management of mockFileContents
    // Alternatively, use one server and pass signals, but that's more complex for map verification.

    // Mock file content store - this will be reset for some scenarios or built upon
    std::map<std::string, std::string> mockFileContents; 
    const std::string testFilePath = "/fuse_write_test.txt";

    auto run_server_for_write_scenario = 
        [&](std::map<std::string, std::string>& currentMockContents) -> Networking::Server* {
        Networking::Server* server = new Networking::Server(testPort); // Heap allocated to return
        EXPECT_TRUE(server->InitServer());

        // Server runs in a new thread for one transaction then finishes
        std::thread([server, &currentMockContents]() {
            try {
                Networking::ClientConnection conn = server->Accept();
                EXPECT_TRUE(conn.clientSocket != 0);

                std::vector<char> rawReq = server->Receive(conn);
                EXPECT_FALSE(rawReq.empty());
                Message reqMsg = Message::Deserialize(std::string(rawReq.begin(), rawReq.end()));
                EXPECT_EQ(reqMsg._Type, MessageType::Write);

                Message respMsg;
                respMsg._Type = MessageType::WriteResponse;
                respMsg._Path = reqMsg._Path;
                respMsg._ErrorCode = 0;
                
                uint64_t offset = reqMsg._Offset;
                const std::string& dataToWrite = reqMsg._Data;
                uint64_t sizeToWrite = dataToWrite.length();
                 // If msg._Size is also set by client for write, it should match dataToWrite.length()
                EXPECT_EQ(reqMsg._Size, sizeToWrite) << "Request _Size field should match _Data.length() for writes.";


                std::string& fileData = currentMockContents[reqMsg._Path]; // Creates file if not exists

                // Ensure fileData is large enough to write at offset
                if (offset > fileData.length()) {
                    fileData.resize(static_cast<size_t>(offset), '\0'); // Pad with nulls
                }
                
                // Perform the write - ensure fileData is large enough for the whole write
                if (offset + sizeToWrite > fileData.length()) {
                    fileData.resize(static_cast<size_t>(offset + sizeToWrite), '\0');
                }
                
                for (size_t i = 0; i < sizeToWrite; ++i) {
                    fileData[static_cast<size_t>(offset) + i] = dataToWrite[i];
                }
                
                respMsg._Size = sizeToWrite; // Bytes written

                server->Send(Message::Serialize(respMsg).c_str(), conn);
                server->DisconnectClient(conn);
            } catch (const Networking::NetworkException& e) {
                ADD_FAILURE() << "Server thread NetworkException: " << e.what();
            } catch (const std::exception& e) {
                ADD_FAILURE() << "Server thread std::exception: " << e.what();
            }
            // server->Shutdown(); // Server is shut down by main thread after join
        }).detach(); // Detach as server lifetime is managed by the caller of lambda
        return server;
    };
    
    auto run_client_for_write_scenario = 
        [&](const std::string& path, uint64_t offset, const std::string& data) {
        Networking::Client client("127.0.0.1", testPort);
        EXPECT_TRUE(client.IsConnected());

        Message writeReq;
        writeReq._Type = MessageType::Write;
        writeReq._Path = path;
        writeReq._Offset = offset;
        writeReq._Data = data;
        writeReq._Size = data.length(); // Client sets _Size to actual data length

        client.Send(Message::Serialize(writeReq).c_str());
        
        std::vector<char> rawResp = client.Receive();
        EXPECT_FALSE(rawResp.empty());
        Message writeResp = Message::Deserialize(std::string(rawResp.begin(), rawResp.end()));

        EXPECT_EQ(writeResp._Type, MessageType::WriteResponse);
        EXPECT_EQ(writeResp._Path, path);
        EXPECT_EQ(writeResp._ErrorCode, 0);
        EXPECT_EQ(writeResp._Size, data.length()) << "Bytes written mismatch.";
        
        client.Disconnect();
    };

    Networking::Server* currentServer = nullptr;

    // Scenario 1: Write to a new file
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FWS: Scenario 1 - Write to new file");
        mockFileContents.clear(); // Ensure fresh state
        currentServer = run_server_for_write_scenario(mockFileContents);
        std::string data = "Hello New File";
        run_client_for_write_scenario(testFilePath, 0, data);
        // Brief sleep for server to process & detach thread to finish its work, then shutdown.
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
        currentServer->Shutdown(); // Shutdown server
        delete currentServer;      // Clean up server object
        ASSERT_EQ(mockFileContents[testFilePath], data);
    }

    // Scenario 2: Overwrite existing file from offset 0
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FWS: Scenario 2 - Overwrite from offset 0");
        mockFileContents[testFilePath] = "Original Content";
        currentServer = run_server_for_write_scenario(mockFileContents);
        std::string data = "Overwritten";
        run_client_for_write_scenario(testFilePath, 0, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown();
        delete currentServer;
        // The overwrite should replace content. If "Overwritten" is shorter, old content might remain.
        // Correct expectation: file is "Overwritten" and original content beyond that is gone if std::string semantics imply truncation on overwrite.
        // Our server logic: resizes to offset + sizeToWrite. So if new data is shorter, file effectively truncates.
        ASSERT_EQ(mockFileContents[testFilePath].substr(0, data.length()), data);
        ASSERT_EQ(mockFileContents[testFilePath].length(), data.length());


    }

    // Scenario 3: Write at an offset within an existing file (overwrite part of it)
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FWS: Scenario 3 - Write at offset");
        mockFileContents[testFilePath] = "Hello World"; // Length 11
        currentServer = run_server_for_write_scenario(mockFileContents);
        std::string data = "FUSE"; // Length 4
        uint64_t offset = 6; // Write "FUSE" over "World"
        run_client_for_write_scenario(testFilePath, offset, data);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown();
        delete currentServer;
        ASSERT_EQ(mockFileContents[testFilePath], "Hello FUSEd"); // Original "World" last 'd' remains if data is shorter than remaining part
                                                              // Our server logic resizes to exactly offset + data.length if that's larger.
                                                              // If offset + data.length is smaller than original, then it's effectively a partial overwrite.
                                                              // "Hello " (6) + "FUSE" (4) = "Hello FUSE". Original length was 11.
                                                              // New length will be offset + data.length = 6 + 4 = 10.
        ASSERT_EQ(mockFileContents[testFilePath], "Hello FUSE");
    }

    // Scenario 4: Append to an existing file
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FWS: Scenario 4 - Append to file");
        std::string initialData = "AppendTest";
        mockFileContents[testFilePath] = initialData;
        currentServer = run_server_for_write_scenario(mockFileContents);
        std::string dataToAppend = "_Suffix";
        run_client_for_write_scenario(testFilePath, initialData.length(), dataToAppend);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown();
        delete currentServer;
        ASSERT_EQ(mockFileContents[testFilePath], initialData + dataToAppend);
    }

    // Scenario 5: Write at an offset beyond current EOF (padding)
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FWS: Scenario 5 - Write beyond EOF (padding)");
        std::string initialData = "Pad"; // Length 3
        mockFileContents[testFilePath] = initialData;
        currentServer = run_server_for_write_scenario(mockFileContents);
        std::string dataToWrite = "Data";
        uint64_t offset = 7; // 3 (Pad) + 4 nulls + Data
        run_client_for_write_scenario(testFilePath, offset, dataToWrite);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown();
        delete currentServer;
        
        std::string expectedContent = initialData;
        expectedContent.resize(static_cast<size_t>(offset), '\0');
        expectedContent += dataToWrite;
        ASSERT_EQ(mockFileContents[testFilePath], expectedContent);
        ASSERT_EQ(mockFileContents[testFilePath].length(), offset + dataToWrite.length());
    }
}

TEST_F(NetworkingTest, FuseReadSimulation) {
    const int testPort = 12393;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    // Mock file content store for the Metaserver simulation
    std::map<std::string, std::string> mockFileContents;
    const std::string testFilePath = "/fuse_read_test.txt";
    const std::string fileContent = "Hello Fuse World, this is a test file content for read simulation.";
    mockFileContents[testFilePath] = fileContent;
    
    // Predefined error code for file not found (example)
    const int METASERVER_ENOENT = 2; // Typically ENOENT is 2

    std::atomic<bool> serverLogicCompleted{false};

    std::thread serverThread([&]() {
        try {
            Networking::ClientConnection conn = server.Accept();
            ASSERT_TRUE(conn.clientSocket != 0);

            Message reqMsg = Message::Deserialize(std::string(server.Receive(conn).begin(), server.Receive(conn).end()));
            ASSERT_EQ(reqMsg._Type, MessageType::Read);

            Message respMsg;
            respMsg._Type = MessageType::ReadResponse;
            respMsg._Path = reqMsg._Path;

            auto it = mockFileContents.find(reqMsg._Path);
            if (it == mockFileContents.end()) {
                respMsg._ErrorCode = METASERVER_ENOENT; // File not found
                respMsg._Size = 0;
            } else {
                const std::string& content = it->second;
                uint64_t offset = reqMsg._Offset;
                uint64_t reqSize = reqMsg._Size;
                
                if (offset >= content.length()) {
                    respMsg._Data = ""; // Read past EOF
                    respMsg._Size = 0;
                    respMsg._ErrorCode = 0; // POSIX read past EOF returns 0 bytes, not an error
                } else {
                    uint64_t availableSize = content.length() - offset;
                    uint64_t actualReadSize = std::min(reqSize, availableSize);
                    respMsg._Data = content.substr(static_cast<size_t>(offset), static_cast<size_t>(actualReadSize));
                    respMsg._Size = actualReadSize;
                    respMsg._ErrorCode = 0;
                }
            }
            server.Send(Message::Serialize(respMsg).c_str(), conn);
            server.DisconnectClient(conn);
        } catch (const Networking::NetworkException& e) {
            FAIL() << "Server thread NetworkException: " << e.what();
        } catch (const std::exception& e) {
            FAIL() << "Server thread std::exception: " << e.what();
        }
        serverLogicCompleted = true;
    });

    Networking::Client client("127.0.0.1", testPort);
    ASSERT_TRUE(client.IsConnected());

    // Scenario 1: Successful Full Read
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 1 - Full Read");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = testFilePath;
        readReq._Offset = 0;
        readReq._Size = fileContent.length();
        
        client.Send(Message::Serialize(readReq).c_str());
        Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));

        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, testFilePath);
        ASSERT_EQ(readResp._ErrorCode, 0);
        ASSERT_EQ(readResp._Size, fileContent.length());
        ASSERT_EQ(readResp._Data, fileContent);
    }

    // Scenario 2: Partial Read (offset and length)
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 2 - Partial Read");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = testFilePath;
        readReq._Offset = 6; // "Fuse World..."
        readReq._Size = 10;  // "Fuse World"
        std::string expectedPartialContent = fileContent.substr(6, 10);

        client.Send(Message::Serialize(readReq).c_str());
        Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));
        
        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, testFilePath);
        ASSERT_EQ(readResp._ErrorCode, 0);
        ASSERT_EQ(readResp._Size, expectedPartialContent.length());
        ASSERT_EQ(readResp._Data, expectedPartialContent);
    }

    // Scenario 3: Read Beyond EOF (offset is past end of file)
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 3 - Read Beyond EOF (offset past EOF)");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = testFilePath;
        readReq._Offset = fileContent.length() + 5;
        readReq._Size = 10;

        client.Send(Message::Serialize(readReq).c_str());
        Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));

        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, testFilePath);
        ASSERT_EQ(readResp._ErrorCode, 0); // POSIX read past EOF returns 0 bytes
        ASSERT_EQ(readResp._Size, 0);
        ASSERT_TRUE(readResp._Data.empty());
    }
    
    // Scenario 4: Read that extends beyond EOF (offset is valid, but size goes over)
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 4 - Read Extends Beyond EOF");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = testFilePath;
        readReq._Offset = fileContent.length() - 5; // Last 5 chars
        readReq._Size = 20; // Request more than available
        std::string expectedPartialContentEOF = fileContent.substr(fileContent.length() - 5);

        client.Send(Message::Serialize(readReq).c_str());
        Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));
        
        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, testFilePath);
        ASSERT_EQ(readResp._ErrorCode, 0);
        ASSERT_EQ(readResp._Size, expectedPartialContentEOF.length());
        ASSERT_EQ(readResp._Data, expectedPartialContentEOF);
         ASSERT_EQ(readResp._Size, 5); // Explicitly check only 5 bytes read
    }

    // Scenario 5: File not found
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 5 - File Not Found");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = "/non_existent_file.txt";
        readReq._Offset = 0;
        readReq._Size = 10;

        client.Send(Message::Serialize(readReq).c_str());
        Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));

        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, "/non_existent_file.txt");
        ASSERT_EQ(readResp._ErrorCode, METASERVER_ENOENT);
        ASSERT_EQ(readResp._Size, 0);
        ASSERT_TRUE(readResp._Data.empty());
    }

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());

    if (serverThread.joinable()) {
        serverThread.join();
    }
    ASSERT_TRUE(serverLogicCompleted.load()) << "Server logic did not complete.";
    server.Shutdown();
}

TEST_F(NetworkingTest, SendReceiveZeroLengthMessages) {
    const int testPort = 12396;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<bool> serverAcceptedClient{false};
    std::atomic<bool> clientAttemptedSendZero{false};
    std::atomic<bool> serverConfirmedEmptyReceive{false};
    std::atomic<bool> clientReadyForServersZeroMsg{false};
    std::atomic<bool> serverAttemptedSendZero{false};
    std::atomic<bool> serverThreadCompleted{false};


    std::thread serverThread([&]() {
        Networking::ClientConnection connection;
        try {
            connection = server.Accept();
            ASSERT_TRUE(connection.clientSocket != 0);
            serverAcceptedClient = true;
            Logger::getInstance().log(LogLevel::DEBUG, "S: Client accepted.");

            // Part 1: Server receives zero-length from client
            int waitRetries = 0;
            while(!clientAttemptedSendZero.load() && waitRetries < 100) { // Max 10s
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }
            ASSERT_TRUE(clientAttemptedSendZero.load()) << "S: Timeout waiting for client to send zero-length message.";
            
            Logger::getInstance().log(LogLevel::DEBUG, "S: Attempting to receive client's zero-length message.");
            std::vector<char> rcvDataClient = server.Receive(connection);
            ASSERT_TRUE(rcvDataClient.empty()) << "S: Received data from client was not empty for zero-length send. Size: " << rcvDataClient.size();
            Logger::getInstance().log(LogLevel::DEBUG, "S: Correctly received empty message from client.");
            serverConfirmedEmptyReceive = true;

            // Part 2: Server sends zero-length to client
            waitRetries = 0;
            while(!clientReadyForServersZeroMsg.load() && waitRetries < 100) { // Max 10s
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }
            ASSERT_TRUE(clientReadyForServersZeroMsg.load()) << "S: Timeout waiting for client to be ready for server's zero-length message.";

            Logger::getInstance().log(LogLevel::DEBUG, "S: Attempting to send zero-length message to client.");
            server.Send("", connection);
            Logger::getInstance().log(LogLevel::DEBUG, "S: Sent zero-length message to client.");
            serverAttemptedSendZero = true;
            
            // Hold connection open a bit for client to receive and assert
            std::this_thread::sleep_for(std::chrono::milliseconds(500));


        } catch (const Networking::NetworkException& e) {
            FAIL() << "S: NetworkException in server thread: " << e.what();
        } catch (const std::exception& e) {
            FAIL() << "S: std::exception in server thread: " << e.what();
        }
        
        if (connection.clientSocket != 0) {
             try { server.DisconnectClient(connection); } catch(...) {}
        }
        serverThreadCompleted = true;
    });

    // Client (Main Thread)
    Networking::Client client("127.0.0.1", testPort);
    int connectRetries = 0;
    while (!client.IsConnected() && connectRetries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (connectRetries > 0) client = Networking::Client("127.0.0.1", testPort);
        connectRetries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "C: Failed to connect to server.";

    int waitRetries = 0;
    while(!serverAcceptedClient.load() && waitRetries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAcceptedClient.load()) << "C: Timeout waiting for server to accept connection.";

    // Part 1: Client sends zero-length to server
    Logger::getInstance().log(LogLevel::DEBUG, "C: Sending zero-length message to server.");
    client.Send("");
    clientAttemptedSendZero = true;
    Logger::getInstance().log(LogLevel::DEBUG, "C: Sent zero-length message to server.");

    waitRetries = 0;
    while(!serverConfirmedEmptyReceive.load() && waitRetries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverConfirmedEmptyReceive.load()) << "C: Timeout waiting for server to confirm empty receive.";
    Logger::getInstance().log(LogLevel::DEBUG, "C: Server confirmed receipt of zero-length message.");

    // Part 2: Client receives zero-length from server
    clientReadyForServersZeroMsg = true;
    Logger::getInstance().log(LogLevel::DEBUG, "C: Ready for server's zero-length message.");

    waitRetries = 0;
    while(!serverAttemptedSendZero.load() && waitRetries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAttemptedSendZero.load()) << "C: Timeout waiting for server to send zero-length message.";
    
    Logger::getInstance().log(LogLevel::DEBUG, "C: Attempting to receive server's zero-length message.");
    std::vector<char> rcvDataServer = client.Receive();
    ASSERT_TRUE(rcvDataServer.empty()) << "C: Received data from server was not empty for zero-length send. Size: " << rcvDataServer.size();
    Logger::getInstance().log(LogLevel::DEBUG, "C: Correctly received empty message from server.");

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());

    if (serverThread.joinable()) {
        serverThread.join();
    }
    ASSERT_TRUE(serverThreadCompleted.load()) << "Server thread did not complete as expected.";
    server.Shutdown();
}

TEST_F(NetworkingTest, SendReceiveLargeMessage) {
    const int testPort = 12395;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    // Define a large message (1MB)
    const size_t largeMessageSize = 1024 * 1024;
    std::string largeMessageStr(largeMessageSize, 'A');
    // Fill with a pattern for more robust checking than just solid 'A's
    for (size_t i = 0; i < largeMessageSize; ++i) {
        largeMessageStr[i] = static_cast<char>('A' + (i % 26));
    }
    // Convert to std::vector<char> for easy comparison with Receive's output if needed,
    // but Send takes const char*, so largeMessageStr.c_str() is primary.
    std::vector<char> largeMessageVec(largeMessageStr.begin(), largeMessageStr.end());


    std::atomic<bool> serverAcceptedClient{false};
    std::atomic<bool> clientAttemptedSendLarge{false};
    std::atomic<bool> serverConfirmedLargeReceive{false};
    std::atomic<bool> serverAttemptedSendLarge{false};
    std::atomic<bool> serverThreadCompleted{false};

    std::thread serverThread([&]() {
        Networking::ClientConnection connection;
        try {
            connection = server.Accept();
            ASSERT_TRUE(connection.clientSocket != 0);
            serverAcceptedClient = true;
            Logger::getInstance().log(LogLevel::DEBUG, "S_LM: Client accepted.");

            // Part 1: Server receives large message from client
            int waitRetries = 0;
            while(!clientAttemptedSendLarge.load() && waitRetries < 200) { // Increased timeout for large data
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }
            ASSERT_TRUE(clientAttemptedSendLarge.load()) << "S_LM: Timeout waiting for client to send large message.";
            
            Logger::getInstance().log(LogLevel::DEBUG, "S_LM: Attempting to receive client's large message.");
            std::vector<char> rcvDataClient = server.Receive(connection);
            ASSERT_EQ(rcvDataClient.size(), largeMessageSize) << "S_LM: Received data size mismatch from client.";
            ASSERT_TRUE(std::equal(rcvDataClient.begin(), rcvDataClient.end(), largeMessageStr.begin())) << "S_LM: Received data content mismatch from client.";
            Logger::getInstance().log(LogLevel::DEBUG, "S_LM: Correctly received and verified large message from client.");
            serverConfirmedLargeReceive = true;

            // Part 2: Server sends large message to client
            Logger::getInstance().log(LogLevel::DEBUG, "S_LM: Attempting to send large message to client.");
            server.Send(largeMessageStr.c_str(), connection);
            Logger::getInstance().log(LogLevel::DEBUG, "S_LM: Sent large message to client.");
            serverAttemptedSendLarge = true;
            
            // Keep connection alive for client to process
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        } catch (const Networking::NetworkException& e) {
            FAIL() << "S_LM: NetworkException in server thread: " << e.what();
        } catch (const std::exception& e) {
            FAIL() << "S_LM: std::exception in server thread: " << e.what();
        }
        
        if (connection.clientSocket != 0) {
             try { server.DisconnectClient(connection); } catch(...) { /* ignore during cleanup */ }
        }
        serverThreadCompleted = true;
    });

    // Client (Main Thread)
    Networking::Client client("127.0.0.1", testPort);
    int connectRetries = 0;
    while (!client.IsConnected() && connectRetries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (connectRetries > 0) client = Networking::Client("127.0.0.1", testPort);
        connectRetries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "C_LM: Failed to connect to server.";

    int waitRetries = 0;
    while(!serverAcceptedClient.load() && waitRetries < 100) { 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAcceptedClient.load()) << "C_LM: Timeout waiting for server to accept connection.";

    // Part 1: Client sends large message to server
    Logger::getInstance().log(LogLevel::DEBUG, "C_LM: Sending large message to server.");
    try {
        client.Send(largeMessageStr.c_str());
    } catch (const Networking::NetworkException& e) {
        FAIL() << "C_LM: NetworkException during client Send: " << e.what();
    }
    clientAttemptedSendLarge = true;
    Logger::getInstance().log(LogLevel::DEBUG, "C_LM: Sent large message to server.");

    waitRetries = 0;
    while(!serverConfirmedLargeReceive.load() && waitRetries < 200) { // Increased timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverConfirmedLargeReceive.load()) << "C_LM: Timeout waiting for server to confirm large receive.";
    Logger::getInstance().log(LogLevel::DEBUG, "C_LM: Server confirmed receipt of large message.");

    // Part 2: Client receives large message from server
    waitRetries = 0;
    while(!serverAttemptedSendLarge.load() && waitRetries < 200) { // Increased timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAttemptedSendLarge.load()) << "C_LM: Timeout waiting for server to send large message.";
    
    Logger::getInstance().log(LogLevel::DEBUG, "C_LM: Attempting to receive server's large message.");
    std::vector<char> rcvDataServer;
    try {
        rcvDataServer = client.Receive();
    } catch (const Networking::NetworkException& e) {
        FAIL() << "C_LM: NetworkException during client Receive: " << e.what();
    }
    ASSERT_EQ(rcvDataServer.size(), largeMessageSize) << "C_LM: Received data size mismatch from server.";
    ASSERT_TRUE(std::equal(rcvDataServer.begin(), rcvDataServer.end(), largeMessageStr.begin())) << "C_LM: Received data content mismatch from server.";
    Logger::getInstance().log(LogLevel::DEBUG, "C_LM: Correctly received and verified large message from server.");

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());

    if (serverThread.joinable()) {
        serverThread.join();
    }
    ASSERT_TRUE(serverThreadCompleted.load()) << "Server thread did not complete as expected (LM Test).";
    server.Shutdown();
}

TEST_F(NetworkingTest, MultipleClientsConcurrentSendReceive) {
    const int testPort = 12394;
    const int numClients = 15; // Moderate number of concurrent clients
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<int> successfulServerOperations{0};
    std::atomic<int> connectedServerHandlers{0}; // To track active server handlers
    std::vector<std::thread> serverWorkerThreads;
    std::atomic<bool> serverReadyToAccept{true}; // Controls server accept loop

    // Server main accept loop (will run in a separate thread to allow main thread to spawn clients)
    std::thread serverAcceptLoopThread([&]() {
        try {
            for (int i = 0; i < numClients; ++i) {
                if (!serverReadyToAccept.load()) break; // Stop if server is shutting down
                Networking::ClientConnection conn = server.Accept();
                if (conn.clientSocket == 0) { // Accept might return invalid if server is shutting down
                     Logger::getInstance().log(LogLevel::WARNING, "S_MC: Accept failed or returned invalid socket, possibly due to shutdown.");
                    if (serverReadyToAccept.load()) { // Only assert if not expecting shutdown
                        FAIL() << "S_MC: Accept failed unexpectedly for client " << i;
                    }
                    break; 
                }
                
                connectedServerHandlers++;
                serverWorkerThreads.emplace_back([&server, conn, i, &successfulServerOperations, &connectedServerHandlers]() {
                    std::string threadIdStr = "S_Worker_C" + std::to_string(i);
                    try {
                        Logger::getInstance().log(LogLevel::DEBUG, threadIdStr + ": Handler started.");
                        std::vector<char> data = server.Receive(conn);
                        ASSERT_FALSE(data.empty()) << threadIdStr << ": Received empty data.";
                        
                        std::string receivedMsg(data.begin(), data.end());
                        std::string expectedMsg = "Hello from Client " + std::to_string(i);
                        ASSERT_EQ(receivedMsg, expectedMsg) << threadIdStr << ": Message content mismatch.";
                        
                        std::string responseMsg = "ACK Client " + std::to_string(i);
                        server.Send(responseMsg.c_str(), conn);
                        
                        successfulServerOperations++;
                        Logger::getInstance().log(LogLevel::DEBUG, threadIdStr + ": Processed successfully.");
                    } catch (const Networking::NetworkException& e) {
                        FAIL() << threadIdStr << ": NetworkException: " << e.what();
                    } catch (const std::exception& e) {
                        FAIL() << threadIdStr << ": std::exception: " << e.what();
                    }
                    // DisconnectClient should be called, ensure it happens even if asserts above fail.
                    // The try-catch for DisconnectClient itself can be added if it's prone to issues on already closed sockets.
                    try {
                        server.DisconnectClient(conn);
                    } catch (const Networking::NetworkException& e) {
                         Logger::getInstance().log(LogLevel::WARNING, threadIdStr + ": NetworkException during DisconnectClient: " + std::string(e.what()));
                    }
                    connectedServerHandlers--;
                });
            }
        } catch (const Networking::NetworkException& e) {
            if (serverReadyToAccept.load()) { // Only fail if not during shutdown
                FAIL() << "S_MC: NetworkException in Accept loop: " << e.what();
            } else {
                 Logger::getInstance().log(LogLevel::INFO, "S_MC: Accept loop caught NetworkException during shutdown: " + std::string(e.what()));
            }
        }
        Logger::getInstance().log(LogLevel::INFO, "S_MC: Accept loop finished.");
    });


    std::vector<std::thread> clientThreads;
    std::atomic<int> successfulClientOperations{0};

    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([i, testPort, &successfulClientOperations]() {
            std::string threadIdStr = "C_Thread_" + std::to_string(i);
            try {
                Logger::getInstance().log(LogLevel::DEBUG, threadIdStr + ": Starting.");
                Networking::Client client("127.0.0.1", testPort);
                int connectRetries = 0;
                while(!client.IsConnected() && connectRetries < 10) { // Retry connection
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    if (connectRetries > 0) client = Networking::Client("127.0.0.1", testPort);
                    connectRetries++;
                }
                ASSERT_TRUE(client.IsConnected()) << threadIdStr << ": Failed to connect.";

                std::string msg = "Hello from Client " + std::to_string(i);
                client.Send(msg.c_str());
                
                std::vector<char> response = client.Receive();
                ASSERT_FALSE(response.empty()) << threadIdStr << ": Received empty response from server.";
                std::string responseStr(response.begin(), response.end());
                
                std::string expectedResponse = "ACK Client " + std::to_string(i);
                ASSERT_EQ(responseStr, expectedResponse) << threadIdStr << ": Response mismatch.";
                
                successfulClientOperations++;
                client.Disconnect();
                ASSERT_FALSE(client.IsConnected()) << threadIdStr << ": Failed to disconnect.";
                Logger::getInstance().log(LogLevel::DEBUG, threadIdStr + ": Completed successfully.");
            } catch (const Networking::NetworkException& e) {
                FAIL() << threadIdStr << ": NetworkException: " << e.what();
            } catch (const std::exception& e) {
                FAIL() << threadIdStr << ": std::exception: " << e.what();
            }
        });
        // Small delay to stagger client connections slightly, if needed, but usually not necessary for this scale.
        // std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    }

    // Join client threads
    for (auto& t : clientThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ASSERT_EQ(successfulClientOperations.load(), numClients) << "Not all client operations completed successfully.";

    // Signal server accept loop to stop (if it hasn't already by processing numClients)
    // and wait for it to finish.
    serverReadyToAccept = false; 
    // server.StopAccepting(); // Ideal if Server class has a method to interrupt Accept() call.
    // If Accept() is blocking, it might need one more connection attempt or server.Shutdown() to unblock.
    // For this test, it's designed to accept numClients and then the serverAcceptLoopThread finishes.
    if(serverAcceptLoopThread.joinable()){
        serverAcceptLoopThread.join();
    }
    
    // Join server worker threads
    for (auto& t : serverWorkerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }

    ASSERT_EQ(successfulServerOperations.load(), numClients) << "Not all server operations completed successfully.";
    ASSERT_EQ(connectedServerHandlers.load(), 0) << "Some server handlers did not complete.";

    server.Shutdown(); // Final server shutdown
}

TEST_F(NetworkingTest, ClientCannotConnectToNonListeningServer) { // Changed to TEST_F
    // Attempt to connect to a port where no server is listening
    Networking::Client client("127.0.0.1", 12340); // Some unlikely port
    ASSERT_FALSE(client.IsConnected()); 
}

TEST_F(NetworkingTest, SendAndReceiveClientToServer) { // Changed to TEST_F
    const int testPort = 12347;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    const char* testMessage = "Hello Server from Client";
    std::string receivedMessage;
    std::thread serverThread([&]() {
        Networking::ClientConnection clientConn = server.Accept();
        ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
        std::vector<char> data = server.Receive(clientConn);
        if (!data.empty()) {
            receivedMessage = std::string(data.begin(), data.end());
        }
        server.DisconnectClient(clientConn);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    Networking::Client client("127.0.0.1", testPort);
    ASSERT_TRUE(client.IsConnected());
    client.Send(testMessage);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Give time for message to be processed
    client.Disconnect();
    if (serverThread.joinable()) serverThread.join(); // Join instead of detach
    server.Shutdown();
    ASSERT_EQ(receivedMessage, testMessage);
}

TEST_F(NetworkingTest, SendAndReceiveServerToClient) { // Changed to TEST_F
    const int testPort = 12348;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    const char* testMessage = "Hello Client from Server";
    std::string receivedMessage;

    std::thread clientThread([&]() {
        Networking::Client client("127.0.0.1", testPort);
        if (client.IsConnected()) {
            std::vector<char> data = client.Receive();
            if (!data.empty()) {
                receivedMessage = std::string(data.begin(), data.end());
            }
            client.Disconnect();
        }
    });
    
    Networking::ClientConnection clientConn = server.Accept();
    ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
    server.Send(testMessage, clientConn);
    
    clientThread.join(); // Wait for client to finish
    server.DisconnectClient(clientConn);
    server.Shutdown();
    
    ASSERT_EQ(receivedMessage, testMessage);
}

TEST_F(NetworkingTest, MultipleClientsConnect) { // Changed to TEST_F
    const int testPort = 12349;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::vector<std::thread> clientThreads;
    const int numClients = 5;
    std::atomic<int> connectedClients{0};

#include <sstream> // Required for std::stringstream

// ... (other includes)

    std::thread serverAcceptThread([&]() {
        for (int i = 0; i < numClients; ++i) {
            try {
                Networking::ClientConnection c = server.Accept();
                ASSERT_TRUE(c.clientSocket != 0);
                server.DisconnectClient(c);
                connectedClients++;
            } catch (const Networking::NetworkException& e) {
                // Server might have been shut down, log and break
                std::stringstream ss;
                ss << "NetworkException in serverAcceptThread: " << e.what();
                Logger::getInstance().log(LogLevel::ERROR, ss.str());
                break;
            }
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([testPort]() {
            Networking::Client client("127.0.0.1", testPort);
            int retries = 0;
            while (!client.IsConnected() && retries < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                client = Networking::Client("127.0.0.1", testPort);
                retries++;
            }
            ASSERT_TRUE(client.IsConnected());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            client.Disconnect();
            ASSERT_FALSE(client.IsConnected());
        });
    }

    for (auto& t : clientThreads) {
        t.join();
    }
    if (serverAcceptThread.joinable()) serverAcceptThread.join(); // Join instead of detach
    server.Shutdown();
    ASSERT_EQ(connectedClients, numClients);
}

TEST_F(NetworkingTest, NodeRegistrationWithMetadataManager) {
    const int metaserverPort = 12350;
    MetadataManager metadataManager; // Metaserver logic instance

    Networking::Server metaserverNetworkListener(metaserverPort);
    ASSERT_TRUE(metaserverNetworkListener.InitServer());

    std::atomic<bool> registrationDone{false};
    std::string registeredNodeId;

    std::thread serverThread([&]() {
        try {
            std::stringstream log_msg_start;
            log_msg_start << "Metaserver listener started on port " << metaserverPort;
            Logger::getInstance().log(LogLevel::INFO, log_msg_start.str());

            Networking::ClientConnection nodeConnection = metaserverNetworkListener.Accept();
            ASSERT_TRUE(nodeConnection.clientSocket != 0);
            Logger::getInstance().log(LogLevel::INFO, "Metaserver accepted connection for registration");

            std::vector<char> data = metaserverNetworkListener.Receive(nodeConnection);
            ASSERT_FALSE(data.empty());
            std::string receivedStr(data.begin(), data.end());
            Message msg = Message::Deserialize(receivedStr);

            ASSERT_EQ(msg._Type, MessageType::RegisterNode);
            if (msg._Type == MessageType::RegisterNode) {
                std::stringstream reg_log_msg;
                reg_log_msg << "Metaserver received RegisterNode message for Node: " << msg._Filename
                            << " on address: " << msg._NodeAddress << ":" << msg._NodePort;
                Logger::getInstance().log(LogLevel::INFO, reg_log_msg.str());

                metadataManager.registerNode(msg._Filename, msg._NodeAddress, msg._NodePort);
                registeredNodeId = msg._Filename; // Store the node ID
                // Send a simple success response
                metaserverNetworkListener.Send("Registered", nodeConnection);
                registrationDone = true;
            } else {
                std::stringstream err_log_msg;
                err_log_msg << "Metaserver received unexpected message type: " << static_cast<int>(msg._Type);
                Logger::getInstance().log(LogLevel::ERROR, err_log_msg.str());
                metaserverNetworkListener.Send("Error: Unexpected message type", nodeConnection);
            }
            metaserverNetworkListener.DisconnectClient(nodeConnection);
            Logger::getInstance().log(LogLevel::INFO, "Metaserver processed registration and disconnected client.");
        } catch (const Networking::NetworkException& e) {
            std::stringstream ss_err;
            ss_err << "Metaserver listener thread NetworkException: " << e.what();
            Logger::getInstance().log(LogLevel::ERROR, ss_err.str());
        } catch (const std::exception& e) {
            std::stringstream ss_err;
            ss_err << "Metaserver listener thread std::exception: " << e.what();
            Logger::getInstance().log(LogLevel::ERROR, ss_err.str());
        }
        // Server will be shut down by the main thread
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server thread time to start listening

    const std::string nodeName = "testNode1";
    // Node's internal server port is different from metaserver's listening port
    Node testNode(nodeName, 12351);
    // The Node::start() method starts its own server and heartbeat thread.
    // For this test, we only need to test registration, so we don't call testNode.start().
    // We directly call registerWithMetadataManager.

    std::stringstream node_reg_log;
    node_reg_log << "Node " << nodeName << " attempting to register with Metaserver on port " << metaserverPort;
    Logger::getInstance().log(LogLevel::INFO, node_reg_log.str());
    testNode.registerWithMetadataManager("127.0.0.1", metaserverPort);

    // Wait for registration to be processed
    int waitRetries = 0;
    while(!registrationDone.load() && waitRetries < 100) { // Max 10 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }

    ASSERT_TRUE(registrationDone.load()) << "Registration was not completed in time.";
    ASSERT_TRUE(metadataManager.isNodeRegistered(nodeName)) << "Node " << nodeName << " was not found in MetadataManager.";
    ASSERT_EQ(registeredNodeId, nodeName) << "The Node ID registered does not match the expected Node ID.";

    Logger::getInstance().log(LogLevel::INFO, "Assertions passed. Shutting down server.");
    metaserverNetworkListener.Shutdown();
    if (serverThread.joinable()) {
        serverThread.join();
    }
    Logger::getInstance().log(LogLevel::INFO, "Test NodeRegistrationWithMetadataManager completed.");
}

TEST_F(NetworkingTest, NodeHeartbeatProcessing) {
    const int metaserverPort = 12352; // Different port for this test
    MetadataManager metadataManager;
    const std::string nodeId = "heartbeatNode";
    const std::string nodeAddr = "127.0.0.1";
    const int nodePort = 7777;

    // 1. Pre-register the node
    metadataManager.registerNode(nodeId, nodeAddr, nodePort);
    NodeInfo initialNodeInfo = metadataManager.getNodeInfo(nodeId);
    ASSERT_TRUE(initialNodeInfo.isAlive);
    time_t initialHeartbeatTime = initialNodeInfo.lastHeartbeat;

    // Small delay to ensure subsequent heartbeat time is different
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Setup server to listen for heartbeat
    Networking::Server metaserverNetworkListener(metaserverPort);
    ASSERT_TRUE(metaserverNetworkListener.InitServer());
    std::atomic<bool> heartbeatProcessed{false};

    std::thread serverThread([&]() {
        try {
            std::stringstream log_msg_start;
            log_msg_start << "Heartbeat listener started on port " << metaserverPort;
            Logger::getInstance().log(LogLevel::INFO, log_msg_start.str());

            Networking::ClientConnection nodeConnection = metaserverNetworkListener.Accept();
            ASSERT_TRUE(nodeConnection.clientSocket != 0);
            Logger::getInstance().log(LogLevel::INFO, "Heartbeat listener accepted connection.");

            std::vector<char> data = metaserverNetworkListener.Receive(nodeConnection);
            ASSERT_FALSE(data.empty());
            std::string receivedStr(data.begin(), data.end());
            Message msg = Message::Deserialize(receivedStr);

            ASSERT_EQ(msg._Type, MessageType::Heartbeat);
            if (msg._Type == MessageType::Heartbeat) {
                std::stringstream hb_log_msg;
                hb_log_msg << "Heartbeat listener received Heartbeat message for Node: " << msg._Filename;
                Logger::getInstance().log(LogLevel::INFO, hb_log_msg.str());
                ASSERT_EQ(msg._Filename, nodeId); // Ensure heartbeat is from the correct node

                metadataManager.processHeartbeat(msg._Filename);
                metaserverNetworkListener.Send("HeartbeatProcessed", nodeConnection);
                heartbeatProcessed = true;
            } else {
                std::stringstream err_log_msg;
                err_log_msg << "Heartbeat listener received unexpected message type: " << static_cast<int>(msg._Type);
                Logger::getInstance().log(LogLevel::ERROR, err_log_msg.str());
                metaserverNetworkListener.Send("Error: Unexpected message type for heartbeat", nodeConnection);
            }
            metaserverNetworkListener.DisconnectClient(nodeConnection);
        } catch (const Networking::NetworkException& e) {
            std::stringstream ss_err;
            ss_err << "Heartbeat listener thread NetworkException: " << e.what();
            Logger::getInstance().log(LogLevel::ERROR, ss_err.str());
        } catch (const std::exception& e) {
            std::stringstream ss_err;
            ss_err << "Heartbeat listener thread std::exception: " << e.what();
            Logger::getInstance().log(LogLevel::ERROR, ss_err.str());
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server thread time to start

    // 3. Simulate Node sending a heartbeat
    try {
        Networking::Client heartbeatClient(nodeAddr.c_str(), metaserverPort); // Connect to our listener
        ASSERT_TRUE(heartbeatClient.IsConnected());

        Message heartbeatMsg;
        heartbeatMsg._Type = MessageType::Heartbeat;
        heartbeatMsg._Filename = nodeId; // Node identifier is sent in _Filename field for heartbeats

        std::string serializedMsg = Message::Serialize(heartbeatMsg);
        heartbeatClient.Send(serializedMsg.c_str());

        // Wait for server to process and send response
        std::vector<char> response = heartbeatClient.Receive();
        ASSERT_FALSE(response.empty());
        std::string responseStr(response.begin(), response.end());
        ASSERT_EQ(responseStr, "HeartbeatProcessed");

        heartbeatClient.Disconnect();
    } catch (const Networking::NetworkException& e) {
        FAIL() << "Heartbeat client failed with NetworkException: " << e.what();
    }


    // Wait for heartbeat to be processed by server thread
    int waitRetries = 0;
    while(!heartbeatProcessed.load() && waitRetries < 50) { // Max 5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(heartbeatProcessed.load()) << "Heartbeat was not processed in time.";

    // 4. Verify heartbeat update
    NodeInfo updatedNodeInfo = metadataManager.getNodeInfo(nodeId);
    ASSERT_TRUE(updatedNodeInfo.isAlive);
    ASSERT_GT(updatedNodeInfo.lastHeartbeat, initialHeartbeatTime) << "Last heartbeat time did not update.";

    // Shutdown
    Logger::getInstance().log(LogLevel::INFO, "Heartbeat test assertions passed. Shutting down server.");
    metaserverNetworkListener.Shutdown();
    if (serverThread.joinable()) {
        serverThread.join();
    }
    Logger::getInstance().log(LogLevel::INFO, "Test NodeHeartbeatProcessing completed.");
}

TEST_F(NetworkingTest, FuseGetattrSimulation) {
    const int metaserverPort = 12353; // Yet another unique port
    MetadataManager metadataManager;

    const std::string testFilePath = "/testfile.txt";
    const uint32_t testFileMode = 0100644; // S_IFREG | 0644
    const uint64_t testFileSize = 0; // Default size after addFile if not set otherwise
    const uint32_t expectedUid = 0; // As per MetadataManager::getFileAttributes
    const uint32_t expectedGid = 0; // As per MetadataManager::getFileAttributes

    // 1. Pre-populate file metadata
    // Register a dummy node so that addFile can succeed.
    metadataManager.registerNode("testnode0", "127.0.0.1", 1234);

    // Now add the file.
    int addFileResult = metadataManager.addFile(testFilePath, {}, testFileMode);
    ASSERT_EQ(addFileResult, 0) << "addFile failed during test setup. Error code: " << addFileResult;
    // fileSizes[testFilePath] should be 0 by default from addFile.

    // 2. Setup server to listen for FUSE GetAttr request
    Networking::Server metaserverNetworkListener(metaserverPort);
    ASSERT_TRUE(metaserverNetworkListener.InitServer());
    std::atomic<bool> requestProcessed{false};

    std::thread serverThread([&]() {
        try {
            std::stringstream log_msg_start;
            log_msg_start << "FUSE GetAttr listener started on port " << metaserverPort;
            Logger::getInstance().log(LogLevel::INFO, log_msg_start.str());

            Networking::ClientConnection fuseAdapterConnection = metaserverNetworkListener.Accept();
            ASSERT_TRUE(fuseAdapterConnection.clientSocket != 0);
            Logger::getInstance().log(LogLevel::INFO, "FUSE GetAttr listener accepted connection.");

            std::vector<char> data = metaserverNetworkListener.Receive(fuseAdapterConnection);
            ASSERT_FALSE(data.empty());
            Message reqMsg = Message::Deserialize(std::string(data.begin(), data.end()));

            ASSERT_EQ(reqMsg._Type, MessageType::GetAttr);
            if (reqMsg._Type == MessageType::GetAttr) {
                Logger::getInstance().log(LogLevel::INFO, "FUSE GetAttr listener received GetAttr request for path: " + reqMsg._Path);
                ASSERT_EQ(reqMsg._Path, testFilePath);

                uint32_t mode, uid, gid;
                uint64_t size;
                int res = metadataManager.getFileAttributes(reqMsg._Path, mode, uid, gid, size);

                Message respMsg;
                respMsg._Type = MessageType::GetAttrResponse;
                respMsg._Path = reqMsg._Path;
                respMsg._ErrorCode = res; // e.g., 0 for success, -ENOENT for not found
                if (res == 0) {
                    respMsg._Mode = mode;
                    respMsg._Uid = uid;
                    respMsg._Gid = gid;
                    respMsg._Size = size;
                }

                metaserverNetworkListener.Send(Message::Serialize(respMsg).c_str(), fuseAdapterConnection);
                requestProcessed = true;
            } else {
                Logger::getInstance().log(LogLevel::ERROR, "FUSE GetAttr listener received unexpected message type.");
                // Consider sending an error response back too
            }
            metaserverNetworkListener.DisconnectClient(fuseAdapterConnection);
        } catch (const std::exception& e) { // Catching std::exception for broader coverage
            std::stringstream ss_err;
            ss_err << "FUSE GetAttr listener thread exception: " << e.what();
            Logger::getInstance().log(LogLevel::ERROR, ss_err.str());
            requestProcessed = true; // Allow main thread to proceed and fail assertions if this was unexpected
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server thread time to start

    // 3. Simulate FUSE adapter client sending request
    Message receivedRespMsg;
    bool clientError = false;
    try {
        Networking::Client fuseClient("127.0.0.1", metaserverPort);
        ASSERT_TRUE(fuseClient.IsConnected());

        Message getattrReqMsg;
        getattrReqMsg._Type = MessageType::GetAttr;
        getattrReqMsg._Path = testFilePath; // Using _Path as per message.h for new FUSE types

        fuseClient.Send(Message::Serialize(getattrReqMsg).c_str());

        std::vector<char> responseData = fuseClient.Receive();
        ASSERT_FALSE(responseData.empty());
        receivedRespMsg = Message::Deserialize(std::string(responseData.begin(), responseData.end()));

        fuseClient.Disconnect();
    } catch (const Networking::NetworkException& e) {
        FAIL() << "FUSE client failed with NetworkException: " << e.what();
        clientError = true;
    } catch (const std::exception& e) {
        FAIL() << "FUSE client failed with std::exception: " << e.what();
        clientError = true;
    }
    ASSERT_FALSE(clientError);


    int waitRetries = 0;
    while(!requestProcessed.load() && waitRetries < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(requestProcessed.load()) << "GetAttr request was not processed by server in time.";

    // 4. Verify response
    ASSERT_EQ(receivedRespMsg._Type, MessageType::GetAttrResponse);
    ASSERT_EQ(receivedRespMsg._ErrorCode, 0); // Expect success
    ASSERT_EQ(receivedRespMsg._Path, testFilePath);
    ASSERT_EQ(receivedRespMsg._Mode, testFileMode);
    ASSERT_EQ(receivedRespMsg._Uid, expectedUid);
    ASSERT_EQ(receivedRespMsg._Gid, expectedGid);
    ASSERT_EQ(receivedRespMsg._Size, testFileSize); // Expecting 0 as per current understanding of addFile/getFileAttributes

    // Shutdown
    Logger::getInstance().log(LogLevel::INFO, "FUSE GetAttr test assertions passed. Shutting down server.");
    metaserverNetworkListener.Shutdown();
    if (serverThread.joinable()) {
        serverThread.join();
    }
    Logger::getInstance().log(LogLevel::INFO, "Test FuseGetattrSimulation completed.");
}

TEST_F(NetworkingTest, ServerInitializationOnSamePort) {
    const int testPort = 12399;
    Networking::Server server1(testPort);
    ASSERT_TRUE(server1.InitServer()) << "First server failed to initialize on port " << testPort;
    ASSERT_TRUE(server1.ServerIsRunning()) << "First server is not running after initialization.";
    ASSERT_EQ(server1.GetPort(), testPort);

    Networking::Server server2(testPort);
    // Expecting InitServer on an already bound port to fail
    // This might return false or throw NetworkException depending on implementation
    bool secondServerInitializedSuccessfully = true;
    try {
        secondServerInitializedSuccessfully = server2.InitServer();
        if (secondServerInitializedSuccessfully) {
            // If it claims success, check if it's actually running (it shouldn't be on the same port)
            // This part of the assertion might be tricky if InitServer returns true but fails silently.
            // However, a robust InitServer should return false or throw.
            ASSERT_FALSE(server2.ServerIsRunning()) << "Second server claims to be running on an already occupied port.";
        }
    } catch (const Networking::NetworkException& e) {
        // This is an acceptable outcome if InitServer throws on port conflict
        std::stringstream ss;
        ss << "Caught expected NetworkException for second server: " << e.what();
        Logger::getInstance().log(LogLevel::INFO, ss.str());
        secondServerInitializedSuccessfully = false; // Explicitly mark as not successful
    }
    
    ASSERT_FALSE(secondServerInitializedSuccessfully) << "Second server initialization did not fail as expected on port " << testPort;

    server1.Shutdown(); // Ensure the first server is properly closed
    // server2 does not need explicit shutdown if InitServer failed or threw.
    // If InitServer returned true but it's not really working, server2.Shutdown() might be needed.
    // However, the primary check is that InitServer itself signals failure.
}

TEST_F(NetworkingTest, ClientSendAfterServerAbruptDisconnect) {
    const int testPort = 12398;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<bool> clientAccepted{false};
    std::thread serverThread([&]() {
        try {
            Networking::ClientConnection conn = server.Accept(); // Accept one connection
            if (conn.clientSocket != 0) { // Successfully accepted
                clientAccepted = true;
                // Keep connection alive until server shuts down.
                // A receive here might block, so we just wait for shutdown.
                while (server.ServerIsRunning() && clientAccepted.load()) { // clientAccepted prevents spinning after shutdown if client disconnects first
                     // Check if client is still connected before attempting to receive
                    if (conn.clientSocket != 0) { // Still valid? Server might have closed it.
                        // Poll or use select for readability if we needed to do more here.
                        // For this test, server shutdown is the primary trigger.
                        std::vector<char> quickCheckData = server.Receive(conn, 1, 10); // Non-blocking check or with timeout
                        if (!quickCheckData.empty() && quickCheckData[0] == 0x04) { // EOT or special signal
                            break; 
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        } catch (const Networking::NetworkException& e) {
            // Expected if server shuts down while Accept is blocking or during Receive
            std::stringstream ss;
            ss << "Server thread caught NetworkException: " << e.what();
            Logger::getInstance().log(LogLevel::INFO, ss.str());
        }
        // Server shutdown will be called from main thread.
    });

    Networking::Client client("127.0.0.1", testPort);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt connection
        retries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "Client failed to connect initially.";
    
    // Wait for server to accept the client
    int accept_retries = 0;
    while(!clientAccepted.load() && accept_retries < 50) { // Max 5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        accept_retries++;
    }
    ASSERT_TRUE(clientAccepted.load()) << "Server did not accept client connection in time.";

    // Abruptly shut down the server
    Logger::getInstance().log(LogLevel::INFO, "Shutting down server for abrupt disconnect test.");
    server.Shutdown();
    clientAccepted = false; // Signal server thread to stop its loop if it was based on this

    bool sendFailedAsExpected = false;
    try {
        // Assuming Send returns bool or int. If void, this part changes.
        // Based on existing tests, Send is `void Client::Send(const char* data)`.
        // So, it must throw on error, or the error is detected later.
        // Let's assume it might throw, or IsConnected will be false after.
        // For now, let's assume Send might not throw immediately but subsequent IsConnected will be false.
        // Or, if send itself encounters an error (like broken pipe), it should throw.

        // Let's refine: The problem statement implies Send() itself should fail.
        // This means Send should either return an error code or throw.
        // If Networking::Client::Send is void and doesn't throw on immediate error,
        // the design might rely on a later Receive or IsConnected check.
        // Let's check `utilities/client.h` for Send's signature and behavior.
        // For now, let's assume Send will throw if the pipe is broken.
        client.Send("Hello after server shutdown");
        // If Send did not throw, we might not have detected the error here.
        // This depends on TCP stack behavior; send might succeed locally if buffer has space.
        // A subsequent receive is a more reliable way to detect a closed connection.
    } catch (const Networking::NetworkException& e) {
        std::stringstream ss;
        ss << "Client caught expected NetworkException during Send: " << e.what();
        Logger::getInstance().log(LogLevel::INFO, ss.str());
        sendFailedAsExpected = true;
    }

    // Attempt to receive data, this should definitely fail or return empty
    std::vector<char> received_data;
    bool receiveFailedAsExpected = false;
    try {
        received_data = client.Receive();
        if (received_data.empty()) {
            // This is an expected outcome if the connection was closed gracefully by the remote peer (server shutdown)
            // or if Receive detects the broken pipe.
            receiveFailedAsExpected = true;
            Logger::getInstance().log(LogLevel::INFO, "Client Receive returned empty data as expected.");
        }
    } catch (const Networking::NetworkException& e) {
        std::stringstream ss;
        ss << "Client caught expected NetworkException during Receive: " << e.what();
        Logger::getInstance().log(LogLevel::INFO, ss.str());
        receiveFailedAsExpected = true;
    }

    // Either send failed, or receive failed, or both.
    // And IsConnected should be false.
    // If Send didn't throw, but the connection is broken, IsConnected should reflect that.
    // A call to Send might return successfully if data is buffered by OS.
    // A subsequent Receive is more likely to detect the broken pipe.
    // Thus, receiveFailedAsExpected is a more reliable indicator of the broken connection for this test.
    ASSERT_TRUE(receiveFailedAsExpected) << "Client Receive did not fail as expected after server shutdown.";
    
    // After a failed send or receive due to server disconnect, IsConnected should be false.
    // It might take an operation (like Send/Receive) for the client to realize the connection is dead.
    ASSERT_FALSE(client.IsConnected()) << "Client IsConnected still true after server shutdown and failed communication attempt.";

    if (serverThread.joinable()) {
        serverThread.join();
    }
    client.Disconnect(); // Ensure client resources are cleaned up
}

TEST_F(NetworkingTest, ServerReceiveAfterClientAbruptDisconnect) {
    const int testPort = 12397;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::atomic<bool> clientAcceptedByServer{false};
    std::atomic<bool> serverReceiveLogicCompleted{false};
    std::atomic<bool> serverReceiveWasEmpty{false};
    std::atomic<bool> serverThrewExceptionOnReceive{false};

    std::thread serverThread([&]() {
        Networking::ClientConnection connection; // Hold the connection
        try {
            connection = server.Accept();
            ASSERT_TRUE(connection.clientSocket != 0) << "Server failed to accept client.";
            clientAcceptedByServer = true;
            Logger::getInstance().log(LogLevel::DEBUG, "Server accepted client. Attempting Receive.");

            std::vector<char> data = server.Receive(connection); // Blocking receive

            if (data.empty()) {
                Logger::getInstance().log(LogLevel::INFO, "Server Receive returned empty vector, as expected due to client disconnect.");
                serverReceiveWasEmpty = true;
            } else {
                // This case should ideally not happen if client disconnected before sending much.
                // However, if client sent data then disconnected, server might receive it.
                // For this test, we expect empty due to abrupt disconnect *during* server's receive wait.
                std::string receivedStr(data.begin(), data.end());
                Logger::getInstance().log(LogLevel::WARNING, "Server Receive got unexpected data: " + receivedStr);
            }
        } catch (const Networking::NetworkException& e) {
            Logger::getInstance().log(LogLevel::INFO, "Server caught NetworkException during Receive, as expected: " + std::string(e.what()));
            serverThrewExceptionOnReceive = true;
        } catch (const std::exception& e) { // Catch other std exceptions for robustness
            Logger::getInstance().log(LogLevel::ERROR, "Server caught std::exception: " + std::string(e.what()));
            FAIL() << "Server thread caught unexpected std::exception: " << e.what();
        }

        // Graceful handling of client disconnection
        if (connection.clientSocket != 0) { // If connection was established
            try {
                Logger::getInstance().log(LogLevel::DEBUG, "Server attempting to disconnect client post-receive attempt.");
                server.DisconnectClient(connection);
                Logger::getInstance().log(LogLevel::INFO, "Server successfully called DisconnectClient.");
            } catch (const Networking::NetworkException& e) {
                Logger::getInstance().log(LogLevel::ERROR, "Server caught NetworkException during DisconnectClient: " + std::string(e.what()));
                // Depending on state, this might be acceptable or not. For now, log as error.
                // If client socket is already closed by client, server's attempt to use it might error.
            }
        }
        serverReceiveLogicCompleted = true;
    });

    // Client actions
    Networking::Client client("127.0.0.1", testPort);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt
        retries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "Client failed to connect.";

    // Wait for server to accept
    int waitRetries = 0;
    while(!clientAcceptedByServer.load() && waitRetries < 50) { // Max 5s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(clientAcceptedByServer.load()) << "Server did not accept client in time.";
    
    // Optional: client sends a tiny bit of data to ensure server is past accept and likely in receive
    // client.Send("SYNC"); 
    // std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Give it a moment to arrive

    Logger::getInstance().log(LogLevel::DEBUG, "Client disconnecting.");
    client.Disconnect(); // Abrupt client disconnect
    ASSERT_FALSE(client.IsConnected()) << "Client failed to disconnect.";

    // Wait for server thread to finish its receive logic
    waitRetries = 0;
    while(!serverReceiveLogicCompleted.load() && waitRetries < 100) { // Max 10s for server to process
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverReceiveLogicCompleted.load()) << "Server receive logic did not complete in time.";

    // Assert server's behavior
    ASSERT_TRUE(serverReceiveWasEmpty.load() || serverThrewExceptionOnReceive.load())
        << "Server Receive neither returned empty nor threw NetworkException. "
        << "WasEmpty: " << serverReceiveWasEmpty.load() 
        << ", ThrewException: " << serverThrewExceptionOnReceive.load();

    if (serverThread.joinable()) {
        serverThread.join();
    }
    server.Shutdown();
}
