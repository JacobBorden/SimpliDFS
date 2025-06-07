#include "gtest/gtest.h"
#include "utilities/client.h"
#include "utilities/server.h"
#include "utilities/networkexception.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <signal.h> // Added for SIGPIPE handling
#include "utilities/logger.h" // Add this include
#include <cstdio>   // For std::remove
#include <string>   // For std::to_string in TearDown
#include <iomanip>  // For std::put_time (timestamps)
#include <sstream>  // For std::ostringstream (timestamps)
#include "metaserver/metaserver.h" // For MetadataManager
#include "node/node.h"             // For Node
#include "utilities/message.h"     // For Message, MessageType
#include <errno.h>

// Helper for timestamp logging in tests
static std::string getTestTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&t);
    oss << std::put_time(&bt, "%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// Basic test fixture for networking tests if needed, or just use TEST directly.
class NetworkingTest : public ::testing::Test {
protected:
    // Optional: Set up resources shared by tests in this fixture
    void SetUp() override {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] NetworkingTest::SetUp Starting." << std::endl;
        try {
            Logger::init(Logger::CONSOLE_ONLY_OUTPUT, LogLevel::DEBUG); // Log to stdout
        } catch (const std::exception& e) {
            std::cerr << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] NetworkingTest::SetUp Logger init failed: " << e.what() << std::endl;
            // Handle or log if SetUp itself fails critically
        }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] NetworkingTest::SetUp Finished." << std::endl;
    }

    // Optional: Clean up shared resources
    void TearDown() override {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] NetworkingTest::TearDown Starting." << std::endl;
        // Removed dummy logger initialization and cleanup to prevent potential issues
        // with global logger state.
        
        // std::remove("networking_tests.log");
        // for (int i = 1; i <= 5; ++i) {
        //     std::remove(("networking_tests.log." + std::to_string(i)).c_str());
        // }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] NetworkingTest::TearDown Finished." << std::endl;
    }
};

TEST_F(NetworkingTest, ServerInitialization) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerInitialization: Starting." << std::endl;
    Networking::Server server(12345);
    ASSERT_TRUE(server.startListening());
    ASSERT_TRUE(server.ServerIsRunning());
    ASSERT_EQ(server.GetPort(), 12345);
    server.Shutdown(); // Ensure server is properly closed
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerInitialization: Finished." << std::endl;
}

TEST_F(NetworkingTest, ClientInitializationAndConnection) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientInitializationAndConnection: Starting." << std::endl;
    Networking::Server server(12346);
    ASSERT_TRUE(server.startListening());
    std::thread serverThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] ServerThread (ClientInit): Started, attempting Accept." << std::endl;
        server.Accept(); // Accept one connection
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] ServerThread (ClientInit): Accept completed." << std::endl;
    });
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientInit): Server thread started, sleeping." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Increased delay to ensure server is listening
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientInit): Attempting client connection." << std::endl;
    Networking::Client client("127.0.0.1", 12346);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) { // More retries
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientInit): Connection attempt " << retries << " failed, retrying." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client = Networking::Client("127.0.0.1", 12346);
        retries++;
    }
    ASSERT_TRUE(client.IsConnected());
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientInit): Client connected. Disconnecting." << std::endl;
    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());
    if (serverThread.joinable()) serverThread.join();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientInit): Server thread joined. Shutting down server." << std::endl;
    server.Shutdown();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientInitializationAndConnection: Finished." << std::endl;
}

TEST_F(NetworkingTest, ClientConnectWithRetryFailure) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientConnectWithRetryFailure: Starting." << std::endl;
    Networking::Client client; // Default constructor

    // Attempt to connect to a port where no server is listening
    // This should trigger the retry logic and eventually fail.
    // Note: This test might take several seconds depending on kMaxRetries and backoff delays.
    // kMaxRetries = 5, kBaseBackoffDelayMs = 200ms. Total time could be ~6.2s + overhead.
    // Consider using a specific port known to be unused, e.g., from ephemeral range but unlikely to be in use.
    const int unlikelyPort = 65530; 
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientConnectWithRetryFailure: Attempting connection to non-existent server 127.0.0.1:" + std::to_string(unlikelyPort) << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Test ClientConnectWithRetryFailure: Attempting connection to non-existent server 127.0.0.1:" + std::to_string(unlikelyPort)); // Old log
    
    ASSERT_FALSE(client.connectWithRetry("127.0.0.1", unlikelyPort)) << "connectWithRetry should return false after all retries to a non-listening port.";
    ASSERT_FALSE(client.IsConnected()) << "Client should not be connected after connectWithRetry failed.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientConnectWithRetryFailure: connectWithRetry correctly returned false. Finished." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Test ClientConnectWithRetryFailure: connectWithRetry correctly returned false."); // Old log
}

TEST_F(NetworkingTest, ClientConnectWithRetrySuccess) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientConnectWithRetrySuccess: Starting." << std::endl;
    const int testPort = 12355; // Unique port for this test
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()) << "Server failed to initialize for retry success test.";

    std::atomic<bool> clientAccepted{false};
    std::thread serverAcceptThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_RetrySuccessTest: Accept thread started, waiting for connection." << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "S_RetrySuccessTest: Accept thread started, waiting for connection."); // Old Log
        try {
            Networking::ClientConnection conn = server.Accept();
            if (conn.clientSocket != 0) {
                clientAccepted = true;
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_RetrySuccessTest: Accepted client connection." << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, "S_RetrySuccessTest: Accepted client connection."); // Old Log
                // Keep connection alive briefly for client to verify, then disconnect.
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                server.DisconnectClient(conn);
            } else {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_RetrySuccessTest: Accept returned invalid socket." << std::endl;
                // Logger::getInstance().log(LogLevel::WARN, "S_RetrySuccessTest: Accept returned invalid socket."); // Old Log
            }
        } catch (const Networking::NetworkException& e) {
            // Log if server accept fails, but primary assertions are on client side.
             std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_RetrySuccessTest: NetworkException in server accept thread: " + std::string(e.what()) << std::endl;
             // Logger::getInstance().log(LogLevel::ERROR, "S_RetrySuccessTest: NetworkException in server accept thread: " + std::string(e.what())); // Old Log
        }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_RetrySuccessTest: Accept thread finished." << std::endl;
    });

    // Give server a moment to start listening
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (RetrySuccess): Server thread started, sleeping." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Networking::Client client;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (RetrySuccess): Attempting connection to 127.0.0.1:" + std::to_string(testPort) << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Test ClientConnectWithRetrySuccess: Attempting connection to 127.0.0.1:" + std::to_string(testPort)); // Old Log
    
    ASSERT_TRUE(client.connectWithRetry("127.0.0.1", testPort)) << "connectWithRetry should return true when server is listening.";
    ASSERT_TRUE(client.IsConnected()) << "Client should be connected after connectWithRetry succeeded.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (RetrySuccess): connectWithRetry correctly returned true and client is connected." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Test ClientConnectWithRetrySuccess: connectWithRetry correctly returned true and client is connected."); // Old Log

    // Wait for server to accept and process the client before client disconnects
    int waitRetries = 0;
    while(!clientAccepted.load() && waitRetries < 50) { // Max 5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(clientAccepted.load()) << "Server did not accept the client connection in time.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (RetrySuccess): Server accepted client. Disconnecting client." << std::endl;

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected()) << "Client failed to disconnect.";

    if (serverAcceptThread.joinable()) {
        serverAcceptThread.join();
    }
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (RetrySuccess): Server thread joined. Shutting down server." << std::endl;
    server.Shutdown();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientConnectWithRetrySuccess: Finished." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Test ClientConnectWithRetrySuccess: Completed."); // Old Log
}

TEST_F(NetworkingTest, FuseMkdirSimulation) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test FuseMkdirSimulation: Starting." << std::endl;
    const int testPort = 12391;
    std::map<std::string, uint32_t> mockDirectories; // path -> mode

    const int METASERVER_EEXIST = 17; // Standard EEXIST
    const int METASERVER_ENOENT = 2;  // Standard ENOENT

    auto get_parent_path = [](const std::string& path) -> std::string {
        // This lambda is part of the test logic, no verbose logs inside typically unless debugging the test itself.
        if (path.empty() || path == "/") return ""; // No parent for root or empty
        size_t last_slash = path.find_last_of('/');
        if (last_slash == 0) return "/"; // Parent is root, e.g., for "/foo"
        if (last_slash == std::string::npos) return ""; // Should not happen for valid absolute paths
        return path.substr(0, last_slash);
    };

    auto run_server_for_mkdir_scenario = 
        [&](std::map<std::string, uint32_t>& currentMockDirs) -> Networking::Server* {
        Networking::Server* server = new Networking::Server(testPort);
        EXPECT_TRUE(server->startListening()); // Call new startListening method
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] run_server_for_mkdir_scenario: Server initialized for port " << testPort << std::endl;

        std::thread([server, &currentMockDirs, get_parent_path]() {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MkdirServerThread: Started." << std::endl;
            try {
                Networking::ClientConnection conn = server->Accept();
                EXPECT_TRUE(conn.clientSocket != 0);
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MkdirServerThread: Accepted connection." << std::endl;

                std::vector<char> rawReq = server->Receive(conn);
                EXPECT_FALSE(rawReq.empty());
                Message reqMsg = Message::Deserialize(std::string(rawReq.begin(), rawReq.end()));
                EXPECT_EQ(reqMsg._Type, MessageType::Mkdir);
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MkdirServerThread: Received Mkdir for " << reqMsg._Path << std::endl;


                Message respMsg;
                respMsg._Type = MessageType::MkdirResponse;
                respMsg._Path = reqMsg._Path;
                respMsg._ErrorCode = 0;

                std::string parentPath = get_parent_path(reqMsg._Path);
                
                if (currentMockDirs.count(reqMsg._Path)) {
                    respMsg._ErrorCode = METASERVER_EEXIST;
                } else {
                    // Path does not exist. Now check parent.
                    if (reqMsg._Path == "/") { 
                        currentMockDirs[reqMsg._Path] = reqMsg._Mode; // Success
                        // Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Created root " + reqMsg._Path + " with mode " + std::to_string(reqMsg._Mode));
                    } else if (parentPath.empty()) {
                        respMsg._ErrorCode = METASERVER_ENOENT; 
                        // Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Denied relative/empty path " + reqMsg._Path);
                    } else if (parentPath == "/") {
                        currentMockDirs[reqMsg._Path] = reqMsg._Mode; // Success
                        // Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Created " + reqMsg._Path + " (parent /) with mode " + std::to_string(reqMsg._Mode));
                    } else {
                        if (currentMockDirs.count(parentPath)) {
                            currentMockDirs[reqMsg._Path] = reqMsg._Mode; // Success
                            // Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Created " + reqMsg._Path + " (parent " + parentPath + ") with mode " + std::to_string(reqMsg._Mode));
                        } else {
                            respMsg._ErrorCode = METASERVER_ENOENT; // Parent does not exist
                            // Logger::getInstance().log(LogLevel::DEBUG, "S_MKDIR: Denied " + reqMsg._Path + ", parent " + parentPath + " does not exist.");
                        }
                    }
                }
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MkdirServerThread: Processed Mkdir for " << reqMsg._Path << ", ErrorCode: " << respMsg._ErrorCode << ". Sending response." << std::endl;
                server->Send(Message::Serialize(respMsg).c_str(), conn);
                server->DisconnectClient(conn);
            } catch (const Networking::NetworkException& e) {
                ADD_FAILURE() << "Server thread NetworkException: " << e.what();
            } catch (const std::exception& e) {
                ADD_FAILURE() << "Server thread std::exception: " << e.what();
            }
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MkdirServerThread: Finished." << std::endl;
        }).detach(); // Detached threads need careful handling in real apps
        return server;
    };

    auto run_client_for_mkdir_scenario = 
        [&](const std::string& path, uint32_t mode, int expectedErrorCode) {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] run_client_for_mkdir_scenario: Client attempting Mkdir for " << path << std::endl;
        Networking::Client client("127.0.0.1", testPort);
        EXPECT_TRUE(client.IsConnected());

        Message mkdirReq;
        mkdirReq._Type = MessageType::Mkdir;
        mkdirReq._Path = path;
        mkdirReq._Mode = mode;

        client.Send(Message::Serialize(mkdirReq).c_str());
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] run_client_for_mkdir_scenario: Sent Mkdir for " << path << ". Awaiting response." << std::endl;
        
        std::vector<char> rawResp = client.Receive();
        EXPECT_FALSE(rawResp.empty());
        Message mkdirResp = Message::Deserialize(std::string(rawResp.begin(), rawResp.end()));
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] run_client_for_mkdir_scenario: Received response for " << path << ", ErrorCode: " << mkdirResp._ErrorCode << std::endl;

        EXPECT_EQ(mkdirResp._Type, MessageType::MkdirResponse);
        EXPECT_EQ(mkdirResp._Path, path);
        EXPECT_EQ(mkdirResp._ErrorCode, expectedErrorCode);
        
        client.Disconnect();
    };
    
    Networking::Server* currentServer = nullptr;
    const uint32_t testMode = 0755;

    // Scenario 1
    {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] FuseMkdirSimulation: Scenario 1 - Successful create /testdir" << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 1 - Successful create /testdir"); // Old log
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

    // Scenario 2
    {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] FuseMkdirSimulation: Scenario 2 - Create existing /testdir" << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 2 - Create existing /testdir"); // Old log
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/testdir", testMode, METASERVER_EEXIST);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/testdir"));
    }

    // Scenario 3
    {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] FuseMkdirSimulation: Scenario 3 - Successful create /testdir/subdir" << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 3 - Successful create /testdir/subdir"); // Old log
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/testdir/subdir", testMode, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_TRUE(mockDirectories.count("/testdir/subdir"));
        if (mockDirectories.count("/testdir/subdir")) {
            ASSERT_EQ(mockDirectories["/testdir/subdir"], testMode);
        }
    }
    
    // Scenario 4
    {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] FuseMkdirSimulation: Scenario 4 - Create /nonexistentparent/newdir" << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 4 - Create /nonexistentparent/newdir"); // Old log
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("/nonexistentparent/newdir", testMode, METASERVER_ENOENT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_FALSE(mockDirectories.count("/nonexistentparent/newdir"));
    }
    
    // Scenario 5
    {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] FuseMkdirSimulation: Scenario 5 - Create relative_dir" << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "C_MKDIR: Scenario 5 - Create relative_dir"); // Old log
        currentServer = run_server_for_mkdir_scenario(mockDirectories);
        run_client_for_mkdir_scenario("relative_dir", testMode, METASERVER_ENOENT);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        currentServer->Shutdown(); delete currentServer;
        ASSERT_FALSE(mockDirectories.count("relative_dir"));
    }
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test FuseMkdirSimulation: Finished." << std::endl;
}

TEST_F(NetworkingTest, ServerShutdownWithActiveClients) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerShutdownWithActiveClients: Starting." << std::endl;
    const int testPort = 12389;
    const int numClients = 3;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::atomic<int> clientsSuccessfullyConnected{0};
    std::atomic<int> serverHandlersStarted{0};
    std::atomic<int> serverHandlersUnblocked{0};
    std::atomic<int> clientsUnblocked{0};
    std::atomic<bool> serverShutdownCalled{false};

    std::vector<std::thread> serverWorkerThreads;
    std::thread serverAcceptLoopThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Started." << std::endl;
        try {
            for (int i = 0; i < numClients; ++i) {
                if (serverShutdownCalled.load()) {
                    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Shutdown called, exiting loop." << std::endl;
                    break;
                }
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Waiting for client " << i << std::endl;
                // Logger::getInstance().log(LogLevel::DEBUG, "S_ACCEPT: Waiting for client " + std::to_string(i)); // Old log
                Networking::ClientConnection conn = server.Accept();
                if (conn.clientSocket == 0) {
                    if (serverShutdownCalled.load()) {
                        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Accept returned invalid socket during shutdown." << std::endl;
                        // Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept returned invalid socket during shutdown."); // Old log
                        break;
                    }
                    FAIL() << "S_ACCEPT: Accept failed for client " << i << " unexpectedly.";
                    break; 
                }
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Accepted client " << i << ". Spawning worker." << std::endl;
                // Logger::getInstance().log(LogLevel::DEBUG, "S_ACCEPT: Accepted client " + std::to_string(i)); // Old log
                
                serverWorkerThreads.emplace_back([&server, conn, i, &serverHandlersStarted, &serverHandlersUnblocked, &serverShutdownCalled]() {
                    std::string logPrefix = "[TEST LOG " + getTestTimestamp() + " TID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "] S_WORKER_C" + std::to_string(i);
                    serverHandlersStarted++;
                    std::cout << logPrefix << ": Started, attempting blocking Receive." << std::endl;
                    // Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Started, attempting blocking Receive."); // Old log
                    try {
                        std::vector<char> data = server.Receive(conn);
                        if (serverShutdownCalled.load()) {
                            ASSERT_TRUE(data.empty()) << logPrefix << ": Receive unblocked but returned data during shutdown.";
                            std::cout << logPrefix << ": Receive unblocked as expected (empty data)." << std::endl;
                            // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Receive unblocked as expected (empty data)."); // Old log
                        } else {
                            std::cout << logPrefix << ": Receive unblocked unexpectedly with data size: " << data.size() << std::endl;
                            // Logger::getInstance().log(LogLevel::WARN, logPrefix + ": Receive unblocked unexpectedly with data size: " + std::to_string(data.size())); // Old log
                        }
                    } catch (const Networking::NetworkException& e) {
                        std::cout << logPrefix << ": Receive caught NetworkException as expected: " << std::string(e.what()) << std::endl;
                        // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Receive caught NetworkException as expected: " + std::string(e.what())); // Old log
                    } catch (const std::exception& e) {
                        FAIL() << logPrefix << ": Receive caught unexpected std::exception: " << e.what();
                    }
                    serverHandlersUnblocked++;
                    std::cout << logPrefix << ": Unblocked and completed." << std::endl;
                    // Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Unblocked and completed."); // Old log
                    try {
                        server.DisconnectClient(conn);
                    } catch (const Networking::NetworkException& e) {
                         std::cout << logPrefix << ": DisconnectClient caught NetworkException (potentially expected): " + std::string(e.what()) << std::endl;
                         // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": DisconnectClient caught NetworkException (potentially expected): " + std::string(e.what())); // Old log
                    }
                });
            }
        } catch (const Networking::NetworkException& e) {
            if (serverShutdownCalled.load()) {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Accept loop caught NetworkException during/after shutdown (expected): " + std::string(e.what()) << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept loop caught NetworkException during/after shutdown (expected): " + std::string(e.what())); // Old log
            } else {
                FAIL() << "S_ACCEPT: NetworkException in Accept loop: " << e.what();
            }
        }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ACCEPT_LOOP (ShutdownTest): Accept loop finished." << std::endl;
        // Logger::getInstance().log(LogLevel::INFO, "S_ACCEPT: Accept loop finished."); // Old log
    });

    std::vector<std::thread> clientThreads;
    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([i, testPort, &clientsSuccessfullyConnected, &clientsUnblocked, &serverShutdownCalled]() {
            std::string logPrefix = "[TEST LOG " + getTestTimestamp() + " TID: " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "] CLIENT_C" + std::to_string(i);
            std::cout << logPrefix << ": Thread started. Attempting connection." << std::endl;
            Networking::Client client("127.0.0.1", testPort);
            int retries = 0;
            while(!client.IsConnected() && retries < 10 && !serverShutdownCalled.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt
                retries++;
            }

            if (!client.IsConnected()) {
                if(serverShutdownCalled.load()){
                    std::cout << logPrefix << ": Could not connect, server already shutting down." << std::endl;
                    // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Could not connect, server already shutting down."); // Old log
                    clientsUnblocked++;
                    return;
                }
                FAIL() << logPrefix << ": Failed to connect.";
                clientsUnblocked++;
                return;
            }
            ASSERT_TRUE(client.IsConnected()) << logPrefix << ": Failed to connect.";
            clientsSuccessfullyConnected++;
            std::cout << logPrefix << ": Connected. Attempting blocking Receive." << std::endl;
            // Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Connected. Attempting blocking Receive."); // Old log

            try {
                std::vector<char> data = client.Receive();
                ASSERT_TRUE(data.empty()) << logPrefix << ": Client Receive unblocked but returned non-empty data. Size: " << data.size();
                std::cout << logPrefix << ": Client Receive unblocked, data empty as expected (graceful shutdown)." << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Client Receive unblocked, data empty as expected (graceful shutdown)."); // Old log
            } catch (const Networking::NetworkException& e) {
                std::cout << logPrefix << ": Client Receive caught NetworkException as expected: " << std::string(e.what()) << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, logPrefix + ": Client Receive caught NetworkException as expected: " + std::string(e.what())); // Old log
            }

            ASSERT_FALSE(client.IsConnected()) << logPrefix << ": IsConnected should be false after server shutdown was detected by Receive.";

            bool sendFailedAsExpected = false;
            try {
                int ret = client.Send("post-shutdown");
                sendFailedAsExpected = (ret < 0);
            } catch (const Networking::NetworkException& e) {
                sendFailedAsExpected = true;
                std::cout << logPrefix << ": Client Send failed as expected after shutdown: " << std::string(e.what()) << std::endl;
            }
            ASSERT_TRUE(sendFailedAsExpected) << logPrefix << ": Client Send did not fail as expected after shutdown.";
            ASSERT_FALSE(client.IsConnected()) << logPrefix << ": IsConnected should remain false.";
            
            clientsUnblocked++;
            std::cout << logPrefix << ": Completed." << std::endl;
            // Logger::getInstance().log(LogLevel::DEBUG, logPrefix + ": Completed."); // Old log
        });
    }

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): All client threads launched. Waiting for connections/handlers to start." << std::endl;
    int wait_retries = 0;
    while((clientsSuccessfullyConnected.load() < numClients || serverHandlersStarted.load() < numClients) && wait_retries < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_retries++;
        if(serverShutdownCalled.load()) break;
    }
    if(clientsSuccessfullyConnected.load() < numClients && !serverShutdownCalled.load()){
         std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): WARN - Not all clients connected successfully before shutdown. Connected: " + std::to_string(clientsSuccessfullyConnected.load()) << std::endl;
         // Logger::getInstance().log(LogLevel::WARN, "Timeout or issue: Not all clients connected successfully before shutdown. Connected: " + std::to_string(clientsSuccessfullyConnected.load())); // Old log
    }
     if(serverHandlersStarted.load() < numClients && !serverShutdownCalled.load()){
         std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): WARN - Not all server handlers started before shutdown. Started: " + std::to_string(serverHandlersStarted.load()) << std::endl;
         // Logger::getInstance().log(LogLevel::WARN, "Timeout or issue: Not all server handlers started before shutdown. Started: " + std::to_string(serverHandlersStarted.load())); // Old log
    }

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): Calling server.Shutdown()." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Calling server.Shutdown()."); // Old log
    serverShutdownCalled = true;
    server.Shutdown();
    ASSERT_FALSE(server.ServerIsRunning()) << "ServerIsRunning should be false after Shutdown.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): server.Shutdown() completed." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: server.Shutdown() completed."); // Old log

    if (serverAcceptLoopThread.joinable()) {
        serverAcceptLoopThread.join();
    }
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): Server accept loop thread joined." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Server accept loop thread joined."); // Old log

    for (auto& t : serverWorkerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ASSERT_EQ(serverHandlersUnblocked.load(), clientsSuccessfullyConnected.load()) << "Not all server handlers unblocked or an incorrect number of handlers were started.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): Server worker threads joined." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Server worker threads joined."); // Old log

    for (auto& t : clientThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    ASSERT_EQ(clientsUnblocked.load(), numClients) << "Not all clients unblocked or completed.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownTest): Client threads joined. Test Finished." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Client threads joined."); // Old log
}

TEST_F(NetworkingTest, ServerShutdownWhileBlockedInAccept) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerShutdownWhileBlockedInAccept: Starting." << std::endl;
    const int testPort = 12388;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::atomic<bool> serverAttemptingAccept{false};
    std::atomic<bool> acceptCallUnblocked{false};
    std::atomic<bool> serverShutdownInProgress{false};
    std::atomic<bool> serverThreadCorrectlyHandledUnblock{false};

    std::thread serverAcceptBlockThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Thread started." << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Thread started."); // Old log
        serverAttemptingAccept = true;
        try {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Calling Accept()." << std::endl;
            // Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Calling Accept()."); // Old log
            Networking::ClientConnection conn = server.Accept();
            acceptCallUnblocked = true;

            if (serverShutdownInProgress.load()) {
                ASSERT_EQ(conn.clientSocket, 0) << "S_AcceptBlock: Accept unblocked during shutdown but returned a valid socket.";
                if (conn.clientSocket == 0) {
                    serverThreadCorrectlyHandledUnblock = true;
                    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Accept unblocked as expected during shutdown, returning invalid socket." << std::endl;
                    // Logger::getInstance().log(LogLevel::INFO, "S_AcceptBlock: Accept unblocked as expected during shutdown, returning invalid socket."); // Old log
                }
            } else {
                if (conn.clientSocket != 0) {
                     std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Accept unblocked and returned a valid client socket unexpectedly." << std::endl;
                     // Logger::getInstance().log(LogLevel::WARN, "S_AcceptBlock: Accept unblocked and returned a valid client socket unexpectedly."); // Old log
                     server.DisconnectClient(conn);
                } else {
                     std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Accept returned invalid socket without shutdown signal." << std::endl;
                     // Logger::getInstance().log(LogLevel::ERROR, "S_AcceptBlock: Accept returned invalid socket without shutdown signal."); // Old log
                }
                FAIL() << "S_AcceptBlock: Accept unblocked unexpectedly without server shutdown signal or with a valid client.";
            }
        } catch (const Networking::NetworkException& e) {
            acceptCallUnblocked = true;
            if (serverShutdownInProgress.load()) {
                serverThreadCorrectlyHandledUnblock = true;
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Accept caught NetworkException as expected during shutdown: " + std::string(e.what()) << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, "S_AcceptBlock: Accept caught NetworkException as expected during shutdown: " + std::string(e.what())); // Old log
            } else {
                FAIL() << "S_AcceptBlock: Accept caught NetworkException unexpectedly: " << e.what();
            }
        } catch (const std::exception& e) {
            acceptCallUnblocked = true;
            FAIL() << "S_AcceptBlock: Accept caught unexpected std::exception: " << e.what();
        }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_AcceptBlock (ShutdownAcceptTest): Thread finished." << std::endl;
        // Logger::getInstance().log(LogLevel::DEBUG, "S_AcceptBlock: Thread finished."); // Old log
    });

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownAcceptTest): Server thread launched. Waiting for it to attempt accept." << std::endl;
    int retries = 0;
    while (!serverAttemptingAccept.load() && retries < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retries++;
    }
    ASSERT_TRUE(serverAttemptingAccept.load()) << "Server thread did not signal that it's attempting to accept.";
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); 

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownAcceptTest): Setting shutdown flag and calling server.Shutdown()." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: Setting shutdown flag and calling server.Shutdown()."); // Old log
    serverShutdownInProgress = true;
    server.Shutdown();

    ASSERT_FALSE(server.ServerIsRunning()) << "ServerIsRunning should be false after Shutdown call.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ShutdownAcceptTest): server.Shutdown() completed." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "TEST_MAIN: server.Shutdown() completed."); // Old log

    if (serverAcceptBlockThread.joinable()) {
        serverAcceptBlockThread.join();
    }
    
    ASSERT_TRUE(acceptCallUnblocked.load()) << "Accept call did not unblock after server shutdown.";
    ASSERT_TRUE(serverThreadCorrectlyHandledUnblock.load()) << "Server thread did not correctly handle the unblocking of Accept.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerShutdownWhileBlockedInAccept: Finished." << std::endl;
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
        EXPECT_TRUE(server->startListening()); // Call new startListening method

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
        EXPECT_TRUE(server->startListening()); // Call new startListening method

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

                // Pad with nulls if writing beyond current EOF
                if (offset > fileData.length()) {
                    fileData.resize(static_cast<size_t>(offset), '\0');
                }

                // Set the file to the exact new size, this handles both extension and truncation.
                fileData.resize(static_cast<size_t>(offset + sizeToWrite)); 
                
                // Place the data
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
    std::cout << "[FRS_MAIN] Initializing server object..." << std::endl;
    Networking::Server server(testPort);
    std::cout << "[FRS_MAIN] Asserting server startListening..." << std::endl;
    ASSERT_TRUE(server.startListening()); // Call new startListening method
    std::cout << "[FRS_MAIN] Server startListening returned true." << std::endl;

    // Mock file content store for the Metaserver simulation
    std::map<std::string, std::string> mockFileContents;
    const std::string testFilePath = "/fuse_read_test.txt";
    const std::string fileContent = "Hello Fuse World, this is a test file content for read simulation.";
    mockFileContents[testFilePath] = fileContent;
    
    // Predefined error code for file not found (example)
    const int METASERVER_ENOENT = 2; // Typically ENOENT is 2

    std::atomic<bool> serverLogicCompleted{false};

    std::thread serverThread([&]() {
        std::cout << "[FRS_SVR] Thread started." << std::endl;
        try {
            std::cout << "[FRS_SVR] Waiting for connection..." << std::endl;
            Networking::ClientConnection conn = server.Accept();
            if (conn.clientSocket != 0) {
                std::cout << "[FRS_SVR] Accepted connection. Socket: " << conn.clientSocket << std::endl;
            } else {
                std::cout << "[FRS_SVR] Accept failed or returned invalid socket." << std::endl;
                ASSERT_TRUE(conn.clientSocket != 0); // Ensure test fails if accept fails
            }
            
            std::cout << "[FRS_SVR] Attempting to receive request..." << std::endl;
            std::vector<char> rawReq = server.Receive(conn);
            std::cout << "[FRS_SVR] Received data size: " << rawReq.size() << std::endl;
            ASSERT_FALSE(rawReq.empty()); // Assuming a read request should not be empty

            std::cout << "[FRS_SVR] Deserializing request..." << std::endl;
            Message reqMsg = Message::Deserialize(std::string(rawReq.begin(), rawReq.end()));
            std::cout << "[FRS_SVR] Deserialized request type: " << static_cast<int>(reqMsg._Type) << std::endl;
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
            std::cout << "[FRS_SVR] Sending response..." << std::endl;
            server.Send(Message::Serialize(respMsg).c_str(), conn);
            std::cout << "[FRS_SVR] Response sent." << std::endl;

            std::cout << "[FRS_SVR] Disconnecting client..." << std::endl;
            server.DisconnectClient(conn);
            std::cout << "[FRS_SVR] Client disconnected." << std::endl;
        } catch (const Networking::NetworkException& e) {
            std::cout << "[FRS_SVR] EXCEPTION: NetworkException: " << e.what() << std::endl;
            FAIL() << "Server thread NetworkException: " << e.what();
        } catch (const std::exception& e) {
            std::cout << "[FRS_SVR] EXCEPTION: StdException: " << e.what() << std::endl;
            FAIL() << "Server thread std::exception: " << e.what();
        }
        std::cout << "[FRS_SVR] Logic completed." << std::endl;
        serverLogicCompleted = true;
    });

    std::cout << "[FRS_CLI] Creating client..." << std::endl;
    Networking::Client client("127.0.0.1", testPort);
    std::cout << "[FRS_CLI] Client created. Checking connection..." << std::endl;
    ASSERT_TRUE(client.IsConnected());
    std::cout << "[FRS_CLI] Client connected." << std::endl;

    // Scenario 1: Successful Full Read
    {
        Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 1 - Full Read");
        Message readReq;
        readReq._Type = MessageType::Read;
        readReq._Path = testFilePath;
        readReq._Offset = 0;
        readReq._Size = fileContent.length();
        
        std::cout << "[FRS_CLI_S1] Sending request..." << std::endl;
        client.Send(Message::Serialize(readReq).c_str());
        std::cout << "[FRS_CLI_S1] Request sent." << std::endl;

        std::cout << "[FRS_CLI_S1] Attempting to receive response..." << std::endl;
        std::vector<char> rawResp = client.Receive();
        std::cout << "[FRS_CLI_S1] Received response data size: " << rawResp.size() << std::endl;
        Message readResp = Message::Deserialize(std::string(rawResp.begin(), rawResp.end()));

        ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
        ASSERT_EQ(readResp._Path, testFilePath);
        ASSERT_EQ(readResp._ErrorCode, 0);
        ASSERT_EQ(readResp._Size, fileContent.length());
        ASSERT_EQ(readResp._Data, fileContent);
    }

    // // Scenario 2: Partial Read (offset and length)
    // {
    //     Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 2 - Partial Read");
    //     Message readReq;
    //     readReq._Type = MessageType::Read;
    //     readReq._Path = testFilePath;
    //     readReq._Offset = 6; // "Fuse World..."
    //     readReq._Size = 10;  // "Fuse World"
    //     std::string expectedPartialContent = fileContent.substr(6, 10);

    //     client.Send(Message::Serialize(readReq).c_str());
    //     Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));
        
    //     ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
    //     ASSERT_EQ(readResp._Path, testFilePath);
    //     ASSERT_EQ(readResp._ErrorCode, 0);
    //     ASSERT_EQ(readResp._Size, expectedPartialContent.length());
    //     ASSERT_EQ(readResp._Data, expectedPartialContent);
    // }

    // // Scenario 3: Read Beyond EOF (offset is past end of file)
    // {
    //     Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 3 - Read Beyond EOF (offset past EOF)");
    //     Message readReq;
    //     readReq._Type = MessageType::Read;
    //     readReq._Path = testFilePath;
    //     readReq._Offset = fileContent.length() + 5;
    //     readReq._Size = 10;

    //     client.Send(Message::Serialize(readReq).c_str());
    //     Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));

    //     ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
    //     ASSERT_EQ(readResp._Path, testFilePath);
    //     ASSERT_EQ(readResp._ErrorCode, 0); // POSIX read past EOF returns 0 bytes
    //     ASSERT_EQ(readResp._Size, 0);
    //     ASSERT_TRUE(readResp._Data.empty());
    // }
    
    // // Scenario 4: Read that extends beyond EOF (offset is valid, but size goes over)
    // {
    //     Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 4 - Read Extends Beyond EOF");
    //     Message readReq;
    //     readReq._Type = MessageType::Read;
    //     readReq._Path = testFilePath;
    //     readReq._Offset = fileContent.length() - 5; // Last 5 chars
    //     readReq._Size = 20; // Request more than available
    //     std::string expectedPartialContentEOF = fileContent.substr(fileContent.length() - 5);

    //     client.Send(Message::Serialize(readReq).c_str());
    //     Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));
        
    //     ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
    //     ASSERT_EQ(readResp._Path, testFilePath);
    //     ASSERT_EQ(readResp._ErrorCode, 0);
    //     ASSERT_EQ(readResp._Size, expectedPartialContentEOF.length());
    //     ASSERT_EQ(readResp._Data, expectedPartialContentEOF);
    //      ASSERT_EQ(readResp._Size, 5); // Explicitly check only 5 bytes read
    // }

    // // Scenario 5: File not found
    // {
    //     Logger::getInstance().log(LogLevel::DEBUG, "C_FRS: Scenario 5 - File Not Found");
    //     Message readReq;
    //     readReq._Type = MessageType::Read;
    //     readReq._Path = "/non_existent_file.txt";
    //     readReq._Offset = 0;
    //     readReq._Size = 10;

    //     client.Send(Message::Serialize(readReq).c_str());
    //     Message readResp = Message::Deserialize(std::string(client.Receive().begin(), client.Receive().end()));

    //     ASSERT_EQ(readResp._Type, MessageType::ReadResponse);
    //     ASSERT_EQ(readResp._Path, "/non_existent_file.txt");
    //     ASSERT_EQ(readResp._ErrorCode, METASERVER_ENOENT);
    //     ASSERT_EQ(readResp._Size, 0);
    //     ASSERT_TRUE(readResp._Data.empty());
    // }

    std::cout << "[FRS_CLI] Disconnecting client..." << std::endl;
    client.Disconnect();
    std::cout << "[FRS_CLI] Client disconnected." << std::endl;
    ASSERT_FALSE(client.IsConnected());

    std::cout << "[FRS_CLI] Joining server thread..." << std::endl;
    if (serverThread.joinable()) {
        serverThread.join();
    }
    std::cout << "[FRS_CLI] Server thread joined." << std::endl;
    ASSERT_TRUE(serverLogicCompleted.load()) << "Server logic did not complete.";
    server.Shutdown();
}

TEST_F(NetworkingTest, SendReceiveZeroLengthMessages) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendReceiveZeroLengthMessages: Starting." << std::endl;
    const int testPort = 12396;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::atomic<bool> serverAcceptedClient{false};
    std::atomic<bool> clientAttemptedSendZero{false};
    std::atomic<bool> serverConfirmedEmptyReceive{false};
    std::atomic<bool> clientReadyForServersZeroMsg{false};
    std::atomic<bool> serverAttemptedSendZero{false};
    std::atomic<bool> serverThreadCompleted{false};


    std::thread serverThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Server thread started." << std::endl;
        Networking::ClientConnection connection{}; // Initialize to ensure clientSocket is 0 if Accept fails early
        try { // Outer try for the entire lambda body
            connection = server.Accept();
            if (connection.clientSocket == 0) {
                 ADD_FAILURE() << "S: Accept failed, clientSocket is 0.";
                 serverThreadCompleted = true; // Mark as completed to avoid join issues if possible
                 return; 
            }
            serverAcceptedClient = true;
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Client accepted." << std::endl;

            // Part 1: Server receives zero-length from client
            int waitRetries = 0;
            while(!clientAttemptedSendZero.load() && waitRetries < 100) { // Max 10s
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }

            if(!clientAttemptedSendZero.load()) { // Use if instead of ASSERT_TRUE to allow cleanup
                 ADD_FAILURE() << "S: Timeout waiting for client to send zero-length message.";
            } else {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Attempting to receive client's zero-length message." << std::endl;
                std::vector<char> rcvDataClient = server.Receive(connection);
                
                if (rcvDataClient.empty()) {
                    serverConfirmedEmptyReceive = true;
                    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Confirmed empty receive from client." << std::endl;
                } else {
                    ADD_FAILURE() << "S_ZeroLen: Received data from client was NOT empty for zero-length send. Size: " << rcvDataClient.size();
                    serverConfirmedEmptyReceive = false; 
                }
            }

            // Part 2: Server sends zero-length to client
            waitRetries = 0;
            while(!clientReadyForServersZeroMsg.load() && waitRetries < 100) { // Max 10s
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }
            if(!clientReadyForServersZeroMsg.load()){
                 ADD_FAILURE() << "S: Timeout waiting for client to be ready for server's zero-length message.";
            } else {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Attempting to send zero-length message to client." << std::endl;
                server.Send("", connection);
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Sent zero-length message to client." << std::endl;
                serverAttemptedSendZero = true;
            }
            
            // Hold connection open a bit for client to receive and assert
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        } catch (const Networking::NetworkException& e) {
            ADD_FAILURE() << "S_ZeroLen: NetworkException in server thread: " << e.what();
        } catch (const std::exception& e) {
            ADD_FAILURE() << "S_ZeroLen: std::exception in server thread: " << e.what();
        } catch (...) {
            ADD_FAILURE() << "S_ZeroLen: Unknown exception in server thread.";
        }
        
        if (connection.clientSocket != 0) {
             try { 
                 std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Attempting to disconnect client in server thread." << std::endl;
                 server.DisconnectClient(connection); 
                 std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Disconnected client in server thread." << std::endl;
             } catch(const std::exception& e) {
                 ADD_FAILURE() << "S_ZeroLen: Exception during DisconnectClient: " << e.what();
             } catch(...) {
                 ADD_FAILURE() << "S_ZeroLen: Unknown exception during DisconnectClient.";
             }
        }
        serverThreadCompleted = true;
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_ZeroLen: Server thread logic completed." << std::endl;
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
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Sending zero-length message to server." << std::endl;
    client.Send("");
    clientAttemptedSendZero = true;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Sent zero-length message to server." << std::endl;

    waitRetries = 0;
    while(!serverConfirmedEmptyReceive.load() && waitRetries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverConfirmedEmptyReceive.load()) << "C_ZeroLen: Timeout waiting for server to confirm empty receive.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Server confirmed receipt of zero-length message." << std::endl;

    // Part 2: Client receives zero-length from server
    clientReadyForServersZeroMsg = true;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Ready for server's zero-length message." << std::endl;

    waitRetries = 0;
    while(!serverAttemptedSendZero.load() && waitRetries < 100) { // Max 10s
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAttemptedSendZero.load()) << "C_ZeroLen: Timeout waiting for server to send zero-length message.";
    
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Attempting to receive server's zero-length message." << std::endl;
    std::vector<char> rcvDataServer = client.Receive();
    ASSERT_TRUE(rcvDataServer.empty()) << "C_ZeroLen: Received data from server was not empty for zero-length send. Size: " << rcvDataServer.size();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Correctly received empty message from server." << std::endl;

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_ZeroLen: Client disconnected." << std::endl;

    if (serverThread.joinable()) {
        serverThread.join();
    }
    ASSERT_TRUE(serverThreadCompleted.load()) << "Server thread did not complete its execution as expected.";
    server.Shutdown();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendReceiveZeroLengthMessages: Finished." << std::endl;
}

TEST_F(NetworkingTest, SendReceiveLargeMessage) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendReceiveLargeMessage: Starting." << std::endl;
    const int testPort = 12395;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

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
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Server thread started." << std::endl;
        Networking::ClientConnection connection{}; // Initialize
        try { // Outer try for the entire lambda body
            connection = server.Accept();
            if (connection.clientSocket == 0) {
                 ADD_FAILURE() << "S_LargeMsg: Accept failed, clientSocket is 0.";
                 serverThreadCompleted = true;
                 return; 
            }
            serverAcceptedClient = true;
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Client accepted." << std::endl;

            // Part 1: Server receives large message from client
            int waitRetries = 0;
            while(!clientAttemptedSendLarge.load() && waitRetries < 200) { // Increased timeout for large data
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                waitRetries++;
            }
            
            if(!clientAttemptedSendLarge.load()) {
                 ADD_FAILURE() << "S_LM: Timeout waiting for client to send large message.";
            } else {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Attempting to receive client's large message." << std::endl;
                std::vector<char> rcvDataClient = server.Receive(connection);
                
                if (rcvDataClient.size() != largeMessageSize) {
                    ADD_FAILURE() << "S_LargeMsg: Received data size mismatch. Expected: " << largeMessageSize << " Got: " << rcvDataClient.size();
                } else if (!std::equal(rcvDataClient.begin(), rcvDataClient.end(), largeMessageStr.begin())) {
                    ADD_FAILURE() << "S_LargeMsg: Received data content mismatch from client.";
                } else {
                    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Correctly received and verified large message from client." << std::endl;
                    serverConfirmedLargeReceive = true; // Set the flag
                }
            }

            // Part 2: Server sends large message to client
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Attempting to send large message to client." << std::endl;
            server.Send(largeMessageStr.c_str(), connection); // This might throw if client disconnected early
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Sent large message to client." << std::endl;
            serverAttemptedSendLarge = true;
            
            // Keep connection alive for client to process
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        } catch (const Networking::NetworkException& e) {
            ADD_FAILURE() << "S_LargeMsg: NetworkException in server thread: " << e.what();
        } catch (const std::exception& e) {
            ADD_FAILURE() << "S_LargeMsg: std::exception in server thread: " << e.what();
        } catch (...) {
            ADD_FAILURE() << "S_LargeMsg: Unknown exception in server thread.";
        }
        
        if (connection.clientSocket != 0) {
             try { 
                 std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Attempting to disconnect client in server thread." << std::endl;
                 server.DisconnectClient(connection); 
                 std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Disconnected client in server thread." << std::endl;
             } catch(const std::exception& e) {
                 ADD_FAILURE() << "S_LargeMsg: Exception during DisconnectClient: " << e.what();
             } catch(...) {
                 ADD_FAILURE() << "S_LargeMsg: Unknown exception during DisconnectClient.";
             }
        }
        serverThreadCompleted = true;
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_LargeMsg: Server thread logic completed." << std::endl;
    });

    // Client (Main Thread)
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Client starting." << std::endl;
    Networking::Client client("127.0.0.1", testPort);
    int connectRetries = 0;
    while (!client.IsConnected() && connectRetries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (connectRetries > 0) client = Networking::Client("127.0.0.1", testPort);
        connectRetries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "C_LM: Failed to connect to server.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Client connected." << std::endl;

    int waitRetries = 0;
    while(!serverAcceptedClient.load() && waitRetries < 100) { 
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAcceptedClient.load()) << "C_LargeMsg: Timeout waiting for server to accept connection.";

    // Part 1: Client sends large message to server
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Sending large message to server." << std::endl;
    try {
        client.Send(largeMessageStr.c_str());
    } catch (const Networking::NetworkException& e) {
        FAIL() << "C_LargeMsg: NetworkException during client Send: " << e.what();
    }
    clientAttemptedSendLarge = true;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Sent large message to server." << std::endl;

    waitRetries = 0;
    while(!serverConfirmedLargeReceive.load() && waitRetries < 200) { // Increased timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverConfirmedLargeReceive.load()) << "C_LargeMsg: Timeout waiting for server to confirm large receive.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Server confirmed receipt of large message." << std::endl;

    // Part 2: Client receives large message from server
    waitRetries = 0;
    while(!serverAttemptedSendLarge.load() && waitRetries < 200) { // Increased timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitRetries++;
    }
    ASSERT_TRUE(serverAttemptedSendLarge.load()) << "C_LargeMsg: Timeout waiting for server to send large message.";
    
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Attempting to receive server's large message." << std::endl;
    std::vector<char> rcvDataServer;
    try {
        rcvDataServer = client.Receive();
    } catch (const Networking::NetworkException& e) {
        FAIL() << "C_LargeMsg: NetworkException during client Receive: " << e.what();
    }
    ASSERT_EQ(rcvDataServer.size(), largeMessageSize) << "C_LargeMsg: Received data size mismatch from server.";
    ASSERT_TRUE(std::equal(rcvDataServer.begin(), rcvDataServer.end(), largeMessageStr.begin())) << "C_LargeMsg: Received data content mismatch from server.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_LargeMsg: Correctly received and verified large message from server." << std::endl;

    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());

    if (serverThread.joinable()) {
        serverThread.join();
    }
    ASSERT_TRUE(serverThreadCompleted.load()) << "Server thread did not complete its execution as expected (LM Test).";
    server.Shutdown();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendReceiveLargeMessage: Finished." << std::endl;
}

TEST_F(NetworkingTest, MultipleClientsConcurrentSendReceive) {
    const int testPort = 12394;
    const int numClients = 15; // Moderate number of concurrent clients
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

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
                     Logger::getInstance().log(LogLevel::WARN, "S_MC: Accept failed or returned invalid socket, possibly due to shutdown.");
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
                        // std::string expectedMsg = "Hello from Client " + std::to_string(i);
                        // ASSERT_EQ(receivedMsg, expectedMsg) << threadIdStr << ": Message content mismatch.";
                        
                        size_t lastSpace = receivedMsg.rfind(' ');
                        ASSERT_NE(lastSpace, std::string::npos) << threadIdStr << ": Could not find space in message from client.";
                        std::string parsedClientIdStr = receivedMsg.substr(lastSpace + 1);

                        std::string responseMsg = "ACK Client " + parsedClientIdStr;
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
                         Logger::getInstance().log(LogLevel::WARN, threadIdStr + ": NetworkException during DisconnectClient: " + std::string(e.what()));
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
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientCannotConnectToNonListeningServer: Starting." << std::endl;
    // Attempt to connect to a port where no server is listening
    try {
        Networking::Client client("127.0.0.1", 12340); // Some unlikely port
        FAIL() << "Expected runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("ECONNREFUSED"), std::string::npos);
    }
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientCannotConnectToNonListeningServer: Finished." << std::endl;
}

TEST_F(NetworkingTest, SendAndReceiveClientToServer) { // Changed to TEST_F
    const int testPort = 12347;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendAndReceiveClientToServer: Starting. Port: " << testPort << std::endl;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    const char* testMessage = "Hello Server from Client";
    std::string receivedMessage;
    std::thread serverThread([&]() {
        Networking::ClientConnection clientConn = server.Accept();
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Thread (ClientToServer): Accepted client. Receiving." << std::endl;
        ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
        std::vector<char> data = server.Receive(clientConn);
        if (!data.empty()) {
            receivedMessage = std::string(data.begin(), data.end());
        }
        server.DisconnectClient(clientConn);
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Thread (ClientToServer): Processed and disconnected." << std::endl;
    });
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientToServer): Server thread launched. Sleeping." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ClientToServer): Client connecting and sending." << std::endl;
    Networking::Client client("127.0.0.1", testPort);
    ASSERT_TRUE(client.IsConnected());
    client.Send(testMessage);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Give time for message to be processed
    client.Disconnect();
    if (serverThread.joinable()) serverThread.join(); // Join instead of detach
    server.Shutdown();
    ASSERT_EQ(receivedMessage, testMessage);
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendAndReceiveClientToServer: Finished." << std::endl;
}

TEST_F(NetworkingTest, SendAndReceiveServerToClient) { // Changed to TEST_F
    const int testPort = 12348;
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendAndReceiveServerToClient: Starting. Port: " << testPort << std::endl;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    const char* testMessage = "Hello Client from Server";
    std::string receivedMessage;

    std::thread clientThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Thread (ServerToClient): Client thread started. Connecting." << std::endl;
        Networking::Client client("127.0.0.1", testPort);
        if (client.IsConnected()) {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Thread (ServerToClient): Connected. Receiving." << std::endl;
            std::vector<char> data = client.Receive();
            if (!data.empty()) {
                receivedMessage = std::string(data.begin(), data.end());
            }
            client.Disconnect();
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Thread (ServerToClient): Received and disconnected." << std::endl;
        }
    });
    
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ServerToClient): Client thread launched. Server accepting." << std::endl;
    Networking::ClientConnection clientConn = server.Accept();
    ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ServerToClient): Server accepted. Sending." << std::endl;
    server.Send(testMessage, clientConn);
    
    clientThread.join(); // Wait for client to finish
    server.DisconnectClient(clientConn);
    server.Shutdown();
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (ServerToClient): Server shutdown." << std::endl;
    
    ASSERT_EQ(receivedMessage, testMessage);
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test SendAndReceiveServerToClient: Finished." << std::endl;
}

TEST_F(NetworkingTest, MultipleClientsConnect) { // Changed to TEST_F
    const int testPort = 12349;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::vector<std::thread> clientThreads;
    const int numClients = 5;
    std::atomic<int> connectedClients{0};

#include <sstream> // Required for std::stringstream

// ... (other includes)
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test MultipleClientsConnect: Starting. Port: " << testPort << std::endl;

    std::thread serverAcceptThread([&]() {
        for (int i = 0; i < numClients; ++i) {
            try {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Thread (MultiClient): Server accepting client " << i << std::endl;
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
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Thread (MultiClient): Server accept loop finished." << std::endl;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    for (int i = 0; i < numClients; ++i) {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MainThread (MultiClient): Launching client thread " << i << std::endl;
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
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test MultipleClientsConnect: Finished." << std::endl;
}

TEST_F(NetworkingTest, NodeRegistrationWithMetadataManager) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test NodeRegistrationWithMetadataManager: Starting." << std::endl;
    const int metaserverPort = 12350;
    MetadataManager metadataManager; // Metaserver logic instance

    Networking::Server metaserverNetworkListener(metaserverPort);
    ASSERT_TRUE(metaserverNetworkListener.startListening()); // Call new startListening method

    std::atomic<bool> registrationDone{false};
    std::string registeredNodeId;

    std::thread serverThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread (NodeReg): Started." << std::endl;
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
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread (NodeReg): Finished." << std::endl;
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
    // Logger::getInstance().log(LogLevel::INFO, "Test NodeRegistrationWithMetadataManager completed."); // Old log
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test NodeRegistrationWithMetadataManager: Finished." << std::endl;
}

TEST_F(NetworkingTest, NodeHeartbeatProcessing) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test NodeHeartbeatProcessing: Starting." << std::endl;
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
    ASSERT_TRUE(metaserverNetworkListener.startListening()); // Call new startListening method
    std::atomic<bool> heartbeatProcessed{false};

    std::thread serverThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] HB_ServerThread: Started." << std::endl;
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
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] HB_ServerThread: Finished." << std::endl;
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
    // Logger::getInstance().log(LogLevel::INFO, "Test NodeHeartbeatProcessing completed."); // Old log
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test NodeHeartbeatProcessing: Finished." << std::endl;
}

TEST_F(NetworkingTest, FuseGetattrSimulation) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test FuseGetattrSimulation: Starting." << std::endl;
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
    metadataManager.registerNode("testnode1", "127.0.0.1", 1235);
    metadataManager.registerNode("testnode2", "127.0.0.1", 1236);
    Networking::Server ns0(1234); ns0.startListening();
    Networking::Server ns1(1235); ns1.startListening();
    Networking::Server ns2(1236); ns2.startListening();
    std::thread t0([&](){auto c=ns0.Accept();ns0.Receive(c);ns0.Send("",c);ns0.DisconnectClient(c);});
    std::thread t1([&](){auto c=ns1.Accept();ns1.Receive(c);ns1.Send("",c);ns1.DisconnectClient(c);});
    std::thread t2([&](){auto c=ns2.Accept();ns2.Receive(c);ns2.Send("",c);ns2.DisconnectClient(c);});

    // Now add the file.
    int addFileResult = metadataManager.addFile(testFilePath, {"testnode0", "testnode1", "testnode2"}, testFileMode);
    ASSERT_EQ(addFileResult, 0) << "addFile failed during test setup. Error code: " << addFileResult;
    // fileSizes[testFilePath] should be 0 by default from addFile.

    // 2. Setup server to listen for FUSE GetAttr request
    Networking::Server metaserverNetworkListener(metaserverPort);
    ASSERT_TRUE(metaserverNetworkListener.startListening()); // Call new startListening method
    std::atomic<bool> requestProcessed{false};

    std::thread serverThread([&]() {
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] GetAttr_ServerThread: Started." << std::endl;
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
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] GetAttr_ServerThread: Exception " << e.what() << std::endl;
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
    ns0.Shutdown(); ns1.Shutdown(); ns2.Shutdown();
    t0.join(); t1.join(); t2.join();
    // Logger::getInstance().log(LogLevel::INFO, "Test FuseGetattrSimulation completed."); // Old log
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test FuseGetattrSimulation: Finished." << std::endl;
}

TEST_F(NetworkingTest, ServerInitializationOnSamePort) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerInitializationOnSamePort: Starting." << std::endl;
    const int testPort = 12399;
    Networking::Server server1(testPort);
    ASSERT_TRUE(server1.startListening()) << "First server failed to initialize on port " << testPort;
    ASSERT_TRUE(server1.ServerIsRunning()) << "First server is not running after initialization.";
    ASSERT_EQ(server1.GetPort(), testPort);

    Networking::Server server2(testPort);
    // Expecting startListening on an already bound port to fail
    bool secondServerStartedSuccessfully = server2.startListening();
    
    ASSERT_FALSE(secondServerStartedSuccessfully) << "Second server initialization (startListening) did not fail as expected on port " << testPort;
    ASSERT_FALSE(server2.ServerIsRunning()) << "Second server should not be running after failed startListening.";


    server1.Shutdown(); // Ensure the first server is properly closed
    // server2 does not need explicit shutdown if startListening failed.
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerInitializationOnSamePort: Finished." << std::endl;
}

TEST_F(NetworkingTest, ClientSendAfterServerAbruptDisconnect) {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE for this test
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientSendAfterServerAbruptDisconnect: Starting." << std::endl;
    const int testPort = 12398;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::atomic<bool> clientAccepted{false};
    std::thread serverThread([&]() {
        try {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (SendTest): Server thread started. Accepting." << std::endl;
            Networking::ClientConnection conn = server.Accept(); // Accept one connection
            if (conn.clientSocket != 0) { // Successfully accepted
                clientAccepted = true;
                // Keep connection alive until server shuts down.
                // A receive here might block, so we just wait for shutdown.
                while (server.ServerIsRunning() && clientAccepted.load()) {
                     // Check if client is still connected before attempting to receive
                    if (conn.clientSocket != 0) {
                        // Poll or use select for readability if we needed to do more here.
                        // For this test, server shutdown is the primary trigger.
                        // A non-blocking receive or receive with timeout would be better here.
                        // For now, this loop will spin until server.IsRunning() is false.
                        // If server.Receive() is blocking, this inner part is problematic for this test's intent.
                        // Let's assume server.Receive() would throw if client is gone or unblock on shutdown.
                        try {
                            std::vector<char> quickCheckData = server.Receive(conn);
                            if (!quickCheckData.empty() && quickCheckData[0] == 0x04) {
                                break;
                            }
                        } catch (const Networking::NetworkException& ) {
                            // Expected if client disconnects or server shuts down during Receive
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        } catch (const Networking::NetworkException& e) {
            // Expected if server shuts down while Accept is blocking
            std::stringstream ss;
            ss << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (SendTest): Server thread caught NetworkException: " << e.what();
            std::cout << ss.str() << std::endl;
        }
        std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (SendTest): Server thread finished." << std::endl;
    });

    Networking::Client client("127.0.0.1", testPort);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt connection
        retries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "Client failed to connect initially.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Client connected. Waiting for server to accept." << std::endl;
    
    // Wait for server to accept the client
    int accept_retries = 0;
    while(!clientAccepted.load() && accept_retries < 50) { // Max 5 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        accept_retries++;
    }
    ASSERT_TRUE(clientAccepted.load()) << "Server did not accept client connection in time.";

    // Abruptly shut down the server
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Server accepted. Shutting down server." << std::endl;
    server.Shutdown();
    clientAccepted = false;

    bool sendFailedAsExpected = false;
    try {
        client.Send("Hello after server shutdown");
    } catch (const Networking::NetworkException& e) {
        std::stringstream ss;
        ss << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Client caught expected NetworkException during Send: " << e.what();
        std::cout << ss.str() << std::endl;
        sendFailedAsExpected = true;
    }
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Client attempting receive after server shutdown." << std::endl;

    std::vector<char> received_data;
    bool receiveFailedAsExpected = false;
    try {
        received_data = client.Receive();
        if (received_data.empty()) {
            receiveFailedAsExpected = true;
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Client Receive returned empty data as expected." << std::endl;
        }
    } catch (const Networking::NetworkException& e) {
        std::stringstream ss;
        ss << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (SendTest): Client caught expected NetworkException during Receive: " << e.what();
        std::cout << ss.str() << std::endl;
        receiveFailedAsExpected = true;
    }

    ASSERT_TRUE(receiveFailedAsExpected) << "Client Receive did not fail as expected after server shutdown.";
    
    ASSERT_FALSE(client.IsConnected()) << "Client IsConnected still true after server shutdown and failed communication attempt.";

    if (serverThread.joinable()) {
        serverThread.join();
    }
    // client.Disconnect(); // Client should already be disconnected or attempting to will do nothing/fail gracefully
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ClientSendAfterServerAbruptDisconnect: Finished." << std::endl;
}

TEST_F(NetworkingTest, ServerReceiveAfterClientAbruptDisconnect) {
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerReceiveAfterClientAbruptDisconnect: Starting." << std::endl;
    const int testPort = 12397;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.startListening()); // Call new startListening method

    std::atomic<bool> clientAcceptedByServer{false};
    std::atomic<bool> serverReceiveLogicCompleted{false};
    std::atomic<bool> serverReceiveWasEmpty{false};
    std::atomic<bool> serverThrewExceptionOnReceive{false};

    std::thread serverThread([&]() {
        Networking::ClientConnection connection; // Hold the connection
        try {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server thread started. Accepting." << std::endl;
            connection = server.Accept();
            ASSERT_TRUE(connection.clientSocket != 0) << "Server failed to accept client.";
            clientAcceptedByServer = true;
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server accepted client. Attempting Receive." << std::endl;
            // Logger::getInstance().log(LogLevel::DEBUG, "Server accepted client. Attempting Receive."); // Old log

            std::vector<char> data = server.Receive(connection); // Blocking receive

            if (data.empty()) {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server Receive returned empty vector, as expected." << std::endl;
                // Logger::getInstance().log(LogLevel::INFO, "Server Receive returned empty vector, as expected due to client disconnect."); // Old log
                serverReceiveWasEmpty = true;
            } else {
                std::string receivedStr(data.begin(), data.end());
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server Receive got unexpected data: " << receivedStr << std::endl;
                // Logger::getInstance().log(LogLevel::WARN, "Server Receive got unexpected data: " + receivedStr); // Old log
            }
        } catch (const Networking::NetworkException& e) {
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server caught NetworkException during Receive, as expected: " + std::string(e.what()) << std::endl;
            // Logger::getInstance().log(LogLevel::INFO, "Server caught NetworkException during Receive, as expected: " + std::string(e.what())); // Old log
            serverThrewExceptionOnReceive = true;
        } catch (const std::exception& e) { // Catch other std exceptions for robustness
            std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server caught std::exception: " + std::string(e.what()) << std::endl;
            // Logger::getInstance().log(LogLevel::ERROR, "Server caught std::exception: " + std::string(e.what())); // Old log
            FAIL() << "Server thread caught unexpected std::exception: " << e.what();
        }

        // Graceful handling of client disconnection
        if (connection.clientSocket != 0) { // If connection was established
            try {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server attempting to disconnect client post-receive attempt." << std::endl;
                server.DisconnectClient(connection);
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server successfully called DisconnectClient." << std::endl;
            } catch (const Networking::NetworkException& e) {
                std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] S_Abrupt (RecvTest): Server caught NetworkException during DisconnectClient: " + std::string(e.what()) << std::endl;
                // Depending on state, this might be acceptable or not. For now, log as error.
                // If client socket is already closed by client, server's attempt to use it might error.
            }
        }
        serverReceiveLogicCompleted = true;
    });

    // Client actions
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (RecvTest): Client starting and connecting." << std::endl;
    Networking::Client client("127.0.0.1", testPort);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (retries > 0) client = Networking::Client("127.0.0.1", testPort); // Reattempt
        retries++;
    }
    ASSERT_TRUE(client.IsConnected()) << "Client failed to connect.";
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (RecvTest): Client connected. Waiting for server to accept." << std::endl;

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

    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] C_Abrupt (RecvTest): Client disconnecting abruptly." << std::endl;
    // Logger::getInstance().log(LogLevel::DEBUG, "Client disconnecting."); // Old log
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
    std::cout << "[TEST LOG " << getTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test ServerReceiveAfterClientAbruptDisconnect: Finished." << std::endl;
}

#include <sys/socket.h> // For socket() and AF_INET6
#include <cerrno>       // For errno

TEST_F(NetworkingTest, IPv6ServerAcceptsConnection) {
    // Preliminary check for IPv6 support
    int testSock = socket(AF_INET6, SOCK_STREAM, 0);
    if (testSock == -1 && errno == EAFNOSUPPORT) {
        GTEST_SKIP() << "IPv6 not supported on this system (EAFNOSUPPORT from preliminary socket check).";
        return; // Ensure the rest of the test doesn't run
    }
    if (testSock != -1) {
        close(testSock); // Close the test socket if successfully created
    }
    // If testSock failed for other reasons, the main test logic below will likely fail and report it.

    const int testPort = 12401;
    Networking::Server server(testPort, Networking::ServerType::IPv6);

    // It's possible startListening itself fails due to EAFNOSUPPORT deeper (e.g. on bind if socket was okay)
    // We could try to catch that, but the Server class currently std::exits on EAFNOSUPPORT in CreateSocket.
    // The preliminary check above is the most direct way to catch socket creation failure.
    // If startListening still causes a FATAL log and exit, this skip won't prevent that part.
    // However, the FATAL log from CreateSocket in Server.cpp for EAFNOSUPPORT will now be preceded by a SKIP if caught by the preliminary check.

    ASSERT_TRUE(server.startListening());

    std::thread serverThread([&]() {
        Networking::ClientConnection conn = server.Accept();
        auto data = server.Receive(conn);
        if (!data.empty()) {
            std::string echo(data.begin(), data.end());
            server.Send(echo.c_str(), conn);
        }
        server.DisconnectClient(conn);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int sock = socket(AF_INET6, SOCK_STREAM, 0);
    ASSERT_NE(sock, -1);
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(testPort);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &addr.sin6_addr), 1);
    ASSERT_EQ(connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    uint32_t len = htonl(2u);
    ASSERT_EQ(send(sock, &len, 4, 0), 4);
    ASSERT_EQ(send(sock, "hi", 2, 0), 2);

    uint32_t respLenNet;
    ASSERT_EQ(recv(sock, &respLenNet, 4, 0), 4);
    uint32_t respLen = ntohl(respLenNet);
    ASSERT_EQ(respLen, 2u);
    char buf[2];
    ASSERT_EQ(recv(sock, buf, 2, 0), 2);
    std::string resp(buf, 2);
    EXPECT_EQ(resp, "hi");

    close(sock);

    if (serverThread.joinable()) serverThread.join();
    server.Shutdown();
}

TEST(ClientTest, ConnectFailureReturnsErrno) {
    uint16_t port = 65530; // assume unused
    try {
        Networking::Client client("127.0.0.1", port);
        FAIL() << "Expected runtime_error";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("ECONNREFUSED"), std::string::npos);
    }
}
