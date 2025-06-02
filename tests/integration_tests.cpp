#include "gtest/gtest.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream> // For std::stringstream

#include "metaserver/metaserver.h"
#include "node/node.h"
#include "utilities/message.h"
#include "utilities/client.h"
#include "utilities/server.h"
#include "utilities/filesystem.h" // Though Node manages its own FS
#include "utilities/logger.h"
#include "utilities/networkexception.h" // For try-catch blocks

// Define unique ports for the test
const int METASERVER_PORT_INTEGRATION = 12360;
const int NODE1_PORT_INTEGRATION = 12361;
const int NODE2_PORT_INTEGRATION = 12362; // If using a second node
const int FUSE_SIM_CLIENT_PORT_INTEGRATION = 12363; // Not strictly needed, client connects

class IntegrationTest : public ::testing::Test {
protected:
    MetadataManager metadataManager; // Each test gets its own manager

    // Synchronization primitives
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<int> nodesRegistered{0};
    std::atomic<bool> fuseRequestProcessed{false};
    std::atomic<int> nodeFilesCreated{0};
    std::string fileCreationTargetNodeId; // To store which node was selected

    void SetUp() override {
        try {
            Logger::init("integration_tests.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            // Log or handle if init fails
        }
        nodesRegistered = 0;
        fuseRequestProcessed = false;
        nodeFilesCreated = 0;
        fileCreationTargetNodeId.clear();
    }

    void TearDown() override {
        // Cleanup any static or global states if necessary, though instance members are preferred
        try {
            Logger::init("dummy_integration_cleanup.log", LogLevel::DEBUG); // Reset logger to avoid interference
            std::remove("dummy_integration_cleanup.log");
            std::remove("dummy_integration_cleanup.log.1");
        } catch (const std::runtime_error& e) { /* ignore */ }
        std::remove("integration_tests.log");
        for (int i = 1; i <= 5; ++i) { // Clean up potential rotated logs
             std::remove(("integration_tests.log." + std::to_string(i)).c_str());
        }
    }
};

TEST_F(IntegrationTest, EndToEndFileCreation) {
    Logger::getInstance().log(LogLevel::INFO, "Starting EndToEndFileCreation test.");

    // --- 1. Start Metaserver Listener Thread ---
    Networking::Server metaserverListener(METASERVER_PORT_INTEGRATION);
    ASSERT_TRUE(metaserverListener.InitServer());
    std::thread metaserverThread([&]() {
        Logger::getInstance().log(LogLevel::INFO, "Metaserver listener thread started.");
        int connectionCount = 0; // Expecting 1 node registration + 1 FUSE request initially

        // For this test, let's assume a specific number of interactions to simplify shutdown.
        // A real server would loop indefinitely.
        while (connectionCount < 2 && metaserverListener.ServerIsRunning()) {
            try {
                Networking::ClientConnection conn = metaserverListener.Accept();
                if (!metaserverListener.ServerIsRunning()) break;

                std::vector<char> data = metaserverListener.Receive(conn);
                if (data.empty()) {
                    Logger::getInstance().log(LogLevel::WARN, "Metaserver received empty data.");
                    metaserverListener.DisconnectClient(conn);
                    continue;
                }
                Message reqMsg = Message::Deserialize(std::string(data.begin(), data.end()));
                connectionCount++;

                if (reqMsg._Type == MessageType::RegisterNode) {
                    Logger::getInstance().log(LogLevel::INFO, "Metaserver: Received RegisterNode from " + reqMsg._Filename);
                    metadataManager.registerNode(reqMsg._Filename, reqMsg._NodeAddress, reqMsg._NodePort);

                    Message ackMsg; // Send simple ack
                    ackMsg._Type = MessageType::RegisterNode; // Echo type
                    ackMsg._ErrorCode = 0;
                    metaserverListener.Send(Message::Serialize(ackMsg).c_str(), conn);
                    {
                        std::lock_guard<std::mutex> lg(mtx);
                        nodesRegistered++;
                    }
                    cv.notify_all();
                } else if (reqMsg._Type == MessageType::CreateFile) {
                    Logger::getInstance().log(LogLevel::INFO, "Metaserver: Received CreateFile for " + reqMsg._Path + " mode " + std::to_string(reqMsg._Mode));
                    std::string filename = reqMsg._Path; // Assuming path is like "/file.txt"
                    if (filename.rfind('/', 0) == 0) { // Starts with '/'
                        filename = filename.substr(1); // Remove leading '/' for internal use
                    }

                    // Metaserver logic: add file, select node(s)
                    int addFileRes = metadataManager.addFile(filename, {}, reqMsg._Mode);
                    Message fuseRespMsg;
                    fuseRespMsg._Type = MessageType::FileCreated; // Or CreateFileResponse
                    fuseRespMsg._Path = reqMsg._Path;

                    if (addFileRes == 0) {
                        std::vector<std::string> targetNodeIds = metadataManager.getFileNodes(filename);
                        ASSERT_FALSE(targetNodeIds.empty()) << "addFile succeeded but no nodes assigned.";
                        fileCreationTargetNodeId = targetNodeIds[0]; // Simplification: use the first node

                        Logger::getInstance().log(LogLevel::INFO, "Metaserver: File " + filename + " assigned to node " + fileCreationTargetNodeId);

                        // Metaserver instructs Node to create the file (empty write)
                        try {
                            std::string nodeFullAddress = metadataManager.getNodeInfo(fileCreationTargetNodeId).nodeAddress;
                            std::string nodeIp = nodeFullAddress.substr(0, nodeFullAddress.find(':'));
                            int nodeInternalPort = std::stoi(nodeFullAddress.substr(nodeFullAddress.find(':') + 1));

                            Networking::Client clientToNode(nodeIp.c_str(), nodeInternalPort);
                            ASSERT_TRUE(clientToNode.IsConnected());

                            Message createFileOrderToNode;
                            createFileOrderToNode._Type = MessageType::WriteFile; // Using WriteFile for create
                            createFileOrderToNode._Filename = filename;
                            createFileOrderToNode._Content = ""; // Empty content for creation

                            clientToNode.Send(Message::Serialize(createFileOrderToNode).c_str());
                            std::vector<char> nodeResponseRaw = clientToNode.Receive();
                            // Node sends string "File ... written successfully."
                            std::string nodeResponseStr(nodeResponseRaw.begin(), nodeResponseRaw.end());
                            Logger::getInstance().log(LogLevel::INFO, "Metaserver: Response from Node " + fileCreationTargetNodeId + " for create: " + nodeResponseStr);

                            if (nodeResponseStr.find("successfully") != std::string::npos) {
                                fuseRespMsg._ErrorCode = 0; // Success
                                {
                                    std::lock_guard<std::mutex> lg(mtx);
                                    nodeFilesCreated++;
                                }
                                cv.notify_all();
                            } else {
                                fuseRespMsg._ErrorCode = -1; // Generic error from node
                            }
                            clientToNode.Disconnect();
                        } catch (const std::exception& e) {
                            Logger::getInstance().log(LogLevel::ERROR, "Metaserver: Failed to command node " + fileCreationTargetNodeId + ": " + e.what());
                            fuseRespMsg._ErrorCode = -1; // Error communicating with node
                        }
                    } else {
                        fuseRespMsg._ErrorCode = addFileRes; // Propagate error from addFile
                    }
                    metaserverListener.Send(Message::Serialize(fuseRespMsg).c_str(), conn);
                    {
                        std::lock_guard<std::mutex> lg(mtx);
                        fuseRequestProcessed = true;
                    }
                    cv.notify_all();
                }
                metaserverListener.DisconnectClient(conn);
            } catch (const Networking::NetworkException& ne) {
                if (!metaserverListener.ServerIsRunning()) {
                    Logger::getInstance().log(LogLevel::INFO, "Metaserver listener shutting down due to NetworkException (likely intended).");
                    break;
                }
                Logger::getInstance().log(LogLevel::ERROR, "Metaserver listener NetworkException: " + std::string(ne.what()));
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, "Metaserver listener std::exception: " + std::string(e.what()));
            }
        }
        Logger::getInstance().log(LogLevel::INFO, "Metaserver listener thread finished.");
    });

    // --- 2. Start Node(s) ---
    // Node's constructor takes (name, port). Node::start() starts its server.
    Node node1("node1", NODE1_PORT_INTEGRATION);
    node1.start(); // Starts listening in a separate thread managed by Node
    Logger::getInstance().log(LogLevel::INFO, "Node1 started.");

    // --- 3. Register Node(s) with Metaserver ---
    Logger::getInstance().log(LogLevel::INFO, "Node1 attempting to register.");
    node1.registerWithMetadataManager("127.0.0.1", METASERVER_PORT_INTEGRATION);

    {
        std::unique_lock<std::mutex> lk(mtx);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(5), [&]{ return nodesRegistered.load() >= 1; }))
            << "Node1 did not register with Metaserver in time.";
    }
    Logger::getInstance().log(LogLevel::INFO, "Node1 registration confirmed by Metaserver.");
    ASSERT_TRUE(metadataManager.isNodeRegistered("node1"));

    // --- 4. Simulate FUSE Client Creating a File ---
    const std::string testFilename = "/newfile.txt";
    const std::string internalFilename = "newfile.txt"; // Path as stored in Metaserver/Node
    const uint32_t testMode = 0100644;
    Message fuseResponse;

    try {
        Logger::getInstance().log(LogLevel::INFO, "FUSE Client: Attempting to create " + testFilename);
        Networking::Client fuseClient("127.0.0.1", METASERVER_PORT_INTEGRATION);
        ASSERT_TRUE(fuseClient.IsConnected());

        Message createFileMsg;
        createFileMsg._Type = MessageType::CreateFile;
        createFileMsg._Path = testFilename; // FUSE client sends full path
        createFileMsg._Mode = testMode;

        fuseClient.Send(Message::Serialize(createFileMsg).c_str());
        std::vector<char> respData = fuseClient.Receive();
        ASSERT_FALSE(respData.empty());
        fuseResponse = Message::Deserialize(std::string(respData.begin(), respData.end()));
        fuseClient.Disconnect();
    } catch (const std::exception& e) {
        FAIL() << "FUSE Client simulation failed: " << e.what();
    }

    {
        std::unique_lock<std::mutex> lk(mtx);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(10), [&]{ return fuseRequestProcessed.load(); }))
            << "Metaserver did not process FUSE request in time.";
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(5), [&]{ return nodeFilesCreated.load() >=1; }))
            << "Node did not confirm file creation in time.";
    }
    Logger::getInstance().log(LogLevel::INFO, "FUSE Client: Received response from Metaserver.");

    // --- 5. Verifications ---
    // a. FUSE client receives success
    ASSERT_EQ(fuseResponse._Type, MessageType::FileCreated); // Or CreateFileResponse
    ASSERT_EQ(fuseResponse._ErrorCode, 0) << "FUSE client did not receive success for CreateFile. Error: " << fuseResponse._ErrorCode;
    ASSERT_EQ(fuseResponse._Path, testFilename);

    // b. MetadataManager has correct metadata
    ASSERT_TRUE(metadataManager.fileExists(internalFilename));
    uint32_t mode, uid, gid;
    uint64_t size;
    ASSERT_EQ(metadataManager.getFileAttributes(internalFilename, mode, uid, gid, size), 0);
    ASSERT_EQ(mode, testMode);

    std::vector<std::string> fileNodes = metadataManager.getFileNodes(internalFilename);
    ASSERT_FALSE(fileNodes.empty());
    ASSERT_EQ(fileNodes[0], fileCreationTargetNodeId); // Check if the correct node was used.
    ASSERT_EQ(fileNodes[0], "node1"); // In this single-node test, it must be node1.


    // c. File exists on the selected Node's filesystem
    ASSERT_FALSE(fileCreationTargetNodeId.empty());
    if (fileCreationTargetNodeId == "node1") {
        ASSERT_TRUE(node1.checkFileExistsOnNode(internalFilename))
            << "File " << internalFilename << " does not exist on node1's filesystem.";
        // Optionally, check content if it were non-empty write
        // std::string contentOnNode = node1.fileSystem.readFile(internalFilename);
        // ASSERT_TRUE(contentOnNode.empty());
    } else {
        FAIL() << "File assigned to unexpected node: " << fileCreationTargetNodeId;
    }

    // --- 6. Shutdown ---
    Logger::getInstance().log(LogLevel::INFO, "Shutting down Metaserver listener.");
    metaserverListener.Shutdown(); // This should make the Accept() in metaserverThread return/throw
    if (metaserverThread.joinable()) {
        metaserverThread.join();
    }
    Logger::getInstance().log(LogLevel::INFO, "Metaserver listener thread joined.");

    // Node's server is shut down by its own destructor or a specific stop method if implemented.
    // For this test, Node's destructor should handle its thread if Node::start() detaches.
    // If Node::start() joins, it would have blocked. Node::start() detaches its threads.
    // We might need an explicit node1.stop() if it exists, or rely on test teardown.
    // The Node class in the project doesn't seem to have an explicit stop, relies on its server object's lifecycle.
    Logger::getInstance().log(LogLevel::INFO, "EndToEndFileCreation test completed.");
}
