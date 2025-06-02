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
