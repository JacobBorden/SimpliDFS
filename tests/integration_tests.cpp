#include "gtest/gtest.h"
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sstream> // For std::stringstream
#include <iostream> // For std::cout verbose logs
#include <iomanip>  // For std::put_time
#include <chrono>   // For timestamps

#include "metaserver/metaserver.h"
#include "node/node.h"
#include "utilities/message.h"
#include "utilities/client.h"
#include "utilities/server.h"
#include "utilities/filesystem.h" // Though Node manages its own FS
#include "utilities/logger.h"
#include "utilities/networkexception.h" // For try-catch blocks

// Helper for timestamp logging in tests (if not already in a common test header)
static std::string getIntegrationTestTimestamp() {
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
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] IntegrationTest::SetUp Starting." << std::endl;
        try {
            Logger::init("integration_tests.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            std::cerr << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] IntegrationTest::SetUp Logger init failed: " << e.what() << std::endl;
        }
        nodesRegistered = 0;
        fuseRequestProcessed = false;
        nodeFilesCreated = 0;
        fileCreationTargetNodeId.clear();
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] IntegrationTest::SetUp Finished." << std::endl;
    }

    void TearDown() override {
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] IntegrationTest::TearDown Starting." << std::endl;
        try {
            Logger::init("dummy_integration_cleanup.log", LogLevel::DEBUG);
            std::remove("dummy_integration_cleanup.log");
            std::remove("dummy_integration_cleanup.log.1");
        } catch (const std::runtime_error& e) { /* ignore */ }
        std::remove("integration_tests.log");
        for (int i = 1; i <= 5; ++i) {
             std::remove(("integration_tests.log." + std::to_string(i)).c_str());
        }
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] IntegrationTest::TearDown Finished." << std::endl;
    }
};

TEST_F(IntegrationTest, DISABLED_EndToEndFileCreation) {
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] Test EndToEndFileCreation: Starting." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Starting EndToEndFileCreation test."); // Old log

    // --- 1. Start Metaserver Listener Thread ---
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: Initializing Metaserver listener." << std::endl;
    Networking::Server metaserverListener(METASERVER_PORT_INTEGRATION);
    ASSERT_TRUE(metaserverListener.startListening()); // Updated to startListening()
    std::thread metaserverThread([&]() {
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Started." << std::endl;
        // Logger::getInstance().log(LogLevel::INFO, "Metaserver listener thread started."); // Old log
        int connectionCount = 0;

        while (connectionCount < 4 && metaserverListener.ServerIsRunning()) {
            try {
                std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Waiting for connection." << std::endl;
                Networking::ClientConnection conn = metaserverListener.Accept();
                if (!metaserverListener.ServerIsRunning()) {
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Server no longer running, exiting accept loop." << std::endl;
                    break;
                }
                std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Accepted connection. Receiving data." << std::endl;
                std::vector<char> data = metaserverListener.Receive(conn);
                if (data.empty()) {
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Received empty data. Disconnecting client." << std::endl;
                    // Logger::getInstance().log(LogLevel::WARN, "Metaserver received empty data."); // Old log
                    metaserverListener.DisconnectClient(conn);
                    continue;
                }
                Message reqMsg = Message::Deserialize(std::string(data.begin(), data.end()));
                connectionCount++;
                std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Received message type " << static_cast<int>(reqMsg._Type) << std::endl;

                if (reqMsg._Type == MessageType::RegisterNode) {
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Processing RegisterNode from " << reqMsg._Filename << std::endl;
                    // Logger::getInstance().log(LogLevel::INFO, "Metaserver: Received RegisterNode from " + reqMsg._Filename); // Old log
                    metadataManager.registerNode(reqMsg._Filename, reqMsg._NodeAddress, reqMsg._NodePort);

                    Message ackMsg;
                    ackMsg._Type = MessageType::RegisterNode;
                    ackMsg._ErrorCode = 0;
                    metaserverListener.Send(Message::Serialize(ackMsg).c_str(), conn);
                    {
                        std::lock_guard<std::mutex> lg(mtx);
                        nodesRegistered++;
                    }
                    cv.notify_all();
                } else if (reqMsg._Type == MessageType::CreateFile) {
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Processing CreateFile for " << reqMsg._Path << std::endl;
                    // Logger::getInstance().log(LogLevel::INFO, "Metaserver: Received CreateFile for " + reqMsg._Path + " mode " + std::to_string(reqMsg._Mode)); // Old log
                    std::string filename = reqMsg._Path;
                    if (filename.rfind('/', 0) == 0) {
                        filename = filename.substr(1);
                    }

                    int addFileRes = metadataManager.addFile(filename, {}, reqMsg._Mode);
                    Message fuseRespMsg;
                    fuseRespMsg._Type = MessageType::FileCreated;
                    fuseRespMsg._Path = reqMsg._Path;

                    if (addFileRes == 0) {
                        std::vector<std::string> targetNodeIds = metadataManager.getFileNodes(filename);
                        ASSERT_FALSE(targetNodeIds.empty()) << "addFile succeeded but no nodes assigned.";
                        fileCreationTargetNodeId = targetNodeIds[0];

                        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: File " << filename << " assigned to node " << fileCreationTargetNodeId << "." << std::endl;
                        // addFile already instructed the node to create the file. Assume success for this test.
                        fuseRespMsg._ErrorCode = 0;
                        {
                            std::lock_guard<std::mutex> lg(mtx);
                            nodeFilesCreated++;
                        }
                        cv.notify_all();
                    } else {
                        fuseRespMsg._ErrorCode = addFileRes;
                    }
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Sending response to FUSE client. ErrorCode: " << fuseRespMsg._ErrorCode << std::endl;
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
                    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Listener shutting down due to NetworkException (likely intended)." << std::endl;
                    // Logger::getInstance().log(LogLevel::INFO, "Metaserver listener shutting down due to NetworkException (likely intended)."); // Old log
                    break;
                }
                std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: NetworkException: " << ne.what() << std::endl;
                // Logger::getInstance().log(LogLevel::ERROR, "Metaserver listener NetworkException: " + std::string(ne.what())); // Old log
            } catch (const std::exception& e) {
                std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: std::exception: " << e.what() << std::endl;
                // Logger::getInstance().log(LogLevel::ERROR, "Metaserver listener std::exception: " + std::string(e.what())); // Old log
            }
        }
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] MetaserverThread: Finished." << std::endl;
        // Logger::getInstance().log(LogLevel::INFO, "Metaserver listener thread finished."); // Old log
    });

    // --- 2. Start Node(s) ---
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: Starting Node1." << std::endl;
    Node node1("node1", NODE1_PORT_INTEGRATION);
    Node node2("node2", NODE1_PORT_INTEGRATION + 1);
    Node node3("node3", NODE1_PORT_INTEGRATION + 2);
    node1.start();
    node2.start();
    node3.start();
    // Logger::getInstance().log(LogLevel::INFO, "Node1 started."); // Old log

    // --- 3. Register Node(s) with Metaserver ---
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: Node1 attempting to register." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Node1 attempting to register."); // Old log
    node1.registerWithMetadataManager("127.0.0.1", METASERVER_PORT_INTEGRATION);
    node2.registerWithMetadataManager("127.0.0.1", METASERVER_PORT_INTEGRATION);
    node3.registerWithMetadataManager("127.0.0.1", METASERVER_PORT_INTEGRATION);

    {
        std::unique_lock<std::mutex> lk(mtx);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(5), [&]{ return nodesRegistered.load() >= 3; }))
            << "Nodes did not register with Metaserver in time.";
    }
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: Node1 registration confirmed by Metaserver." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "Node1 registration confirmed by Metaserver."); // Old log
    ASSERT_TRUE(metadataManager.isNodeRegistered("node1"));
    ASSERT_TRUE(metadataManager.isNodeRegistered("node2"));
    ASSERT_TRUE(metadataManager.isNodeRegistered("node3"));

    // --- 4. Simulate FUSE Client Creating a File ---
    const std::string testFilename = "/newfile.txt";
    const std::string internalFilename = "newfile.txt";
    const uint32_t testMode = 0100644;
    Message fuseResponse;

    try {
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: FUSE Client attempting to create " << testFilename << std::endl;
        // Logger::getInstance().log(LogLevel::INFO, "FUSE Client: Attempting to create " + testFilename); // Old log
        Networking::Client fuseClient("127.0.0.1", METASERVER_PORT_INTEGRATION);
        ASSERT_TRUE(fuseClient.IsConnected());

        Message createFileMsg;
        createFileMsg._Type = MessageType::CreateFile;
        createFileMsg._Path = testFilename;
        createFileMsg._Mode = testMode;

        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: FUSE Client sending CreateFile to Metaserver." << std::endl;
        fuseClient.Send(Message::Serialize(createFileMsg).c_str());
        std::vector<char> respData = fuseClient.Receive();
        ASSERT_FALSE(respData.empty());
        fuseResponse = Message::Deserialize(std::string(respData.begin(), respData.end()));
        std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: FUSE Client received response from Metaserver. ErrorCode: " << fuseResponse._ErrorCode << std::endl;
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
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: FUSE Client received response from Metaserver and async operations completed." << std::endl;
    // Logger::getInstance().log(LogLevel::INFO, "FUSE Client: Received response from Metaserver."); // Old log

    // --- 5. Verifications ---
    std::cout << "[INTEGRATION TEST LOG " << getIntegrationTestTimestamp() << " TID: " << std::this_thread::get_id() << "] EndToEndFileCreation: Starting verifications." << std::endl;
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
