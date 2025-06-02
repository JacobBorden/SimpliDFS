#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "node/node.h" // Assuming Node class definition is here
#include "mocks/mock_filesystem.h"
#include "mocks/mock_networking.h" // For standalone MockNetClient
#include "utilities/message.h"
#include "utilities/logger.h" // For logger initialization
#include <memory> // For std::shared_ptr or std::unique_ptr

// Test fixture for Node tests
class NodeTest : public ::testing::Test {
protected:
    std::string testNodeName = "testNode1";
    int testPort = 12345;
    // The actual Node class takes port in constructor and creates its own Networking::Server
    // To test handleClient, we might need to inject a way to feed it ClientConnection objects
    // or test it more indirectly if it's hard to isolate.

    // For testing ReplicateFileCommand's client part:
    std::shared_ptr<MockNetClient> mockReplicationClient;
    // For file operations:
    std::shared_ptr<MockFileSystem> mockFs;

    NodeTest() {
        // Initialize logger for tests to avoid issues if components use it
        try {
            Logger::init("node_test.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            // No-op if already initialized or fails, tests might log to console instead
        }
        mockReplicationClient = std::make_shared<MockNetClient>();
        mockFs = std::make_shared<MockFileSystem>();
    }

    // We'd need a way to make the Node class use our mockFs.
    // If Node's FileSystem is not injectable, we can't directly mock it.
    // Let's assume for now we can test parts of handleClient logic that use FileSystem
    // by checking messages, or that Node could be refactored for DI.
    // Same for Networking::Client used in ReplicateFileCommand.
};

// Placeholder test
TEST_F(NodeTest, Placeholder) {
    ASSERT_TRUE(true);
}

// Example: Test for ReceiveFileCommand
// To test Node::handleClient, we would need to:
// 1. Create a Node instance.
// 2. Create a mock ClientConnection (or simulate one).
// 3. Create a Message for ReceiveFileCommand.
// 4. Call node.handleClient(mock_connection, serialized_message_bytes).
// 5. Check the response sent back via the mock_connection's server part.

// For ReplicateFileCommand, it's more complex as it involves:
// - Reading from its own FileSystem (use MockFileSystem).
// - Acting as a client to another node (use MockNetClient for this outgoing connection).

// Test processReceiveFileCommand method
TEST_F(NodeTest, ProcessReceiveFileCommand) {
    Node node(testNodeName, testPort); // Real node, but FileSystem is not injected yet.
                                       // For this test, FileSystem is not used by ReceiveFileCommand.
    Message input_msg;
    input_msg._Type = MessageType::ReceiveFileCommand;
    input_msg._Filename = "testfile.txt";
    input_msg._NodeAddress = "127.0.0.1:54321";

    Message response_msg = node.processReceiveFileCommand(input_msg);

    EXPECT_EQ(response_msg._Type, MessageType::ReceiveFileCommand);
    EXPECT_EQ(response_msg._ErrorCode, 0);
    EXPECT_EQ(response_msg._Filename, "testfile.txt");
}

// Test processReadFileRequest method
TEST_F(NodeTest, ProcessReadFileRequest_FileExists) {
    // To test this properly, Node should allow FileSystem injection.
    // Assuming Node has a way to set its FileSystem member to mockFs, or it's passed to processReadFileRequest.
    // For now, let's create a temporary Node and assume its internal FileSystem can be mocked,
    // or we test the logic by checking the response based on mockFs behavior.
    // This test requires Node to be constructed with or allow setting of a mock FileSystem.
    // Let's modify Node to accept FileSystem in constructor for testing. (This would be a prior refactoring step)
    // For now, we can't directly use mockFs with the current Node constructor.
    // This test will be conceptual for `processReadFileRequest`'s logic if `fileSystem` was our `mockFs`.

    // Conceptual:
    // Node node_with_mock_fs(testNodeName, testPort, mockFs); // Assumes constructor injection
    // EXPECT_CALL(*mockFs, fileExists("existingfile.txt")).WillOnce(testing::Return(true));
    // EXPECT_CALL(*mockFs, readFile("existingfile.txt")).WillOnce(testing::Return("file content"));
    // Message input_msg; input_msg._Type = MessageType::ReadFile; input_msg._Filename = "existingfile.txt";
    // Message res = node_with_mock_fs.processReadFileRequest(input_msg);
    // EXPECT_EQ(res._ErrorCode, 0); EXPECT_EQ(res._Data, "file content"); ...

    // Test with real FileSystem for now, acknowledging limitation for mocking.
    Node node(testNodeName, testPort); // Uses real FileSystem
    // Create a file in the real FileSystem first
    node.processWriteFileRequest(Message{MessageType::WriteFile, "realfile.txt", "real content"});

    Message read_req;
    read_req._Type = MessageType::ReadFile;
    read_req._Filename = "realfile.txt";
    Message read_res = node.processReadFileRequest(read_req);

    EXPECT_EQ(read_res._Type, MessageType::ReadFileResponse);
    EXPECT_EQ(read_res._ErrorCode, 0);
    EXPECT_EQ(read_res._Data, "real content");
    EXPECT_EQ(read_res._Size, strlen("real content"));
}

TEST_F(NodeTest, ProcessReadFileRequest_FileDoesNotExist) {
    Node node(testNodeName, testPort); // Uses real FileSystem
    Message read_req;
    read_req._Type = MessageType::ReadFile;
    read_req._Filename = "nonexistentfile.txt";
    Message read_res = node.processReadFileRequest(read_req);

    EXPECT_EQ(read_res._Type, MessageType::ReadFileResponse);
    EXPECT_EQ(read_res._ErrorCode, ENOENT);
    EXPECT_TRUE(read_res._Data.empty());
}

// Test processWriteFileRequest method
TEST_F(NodeTest, ProcessWriteFileRequest) {
    Node node(testNodeName, testPort); // Uses real FileSystem
    Message write_req;
    write_req._Type = MessageType::WriteFile;
    write_req._Filename = "newfiletowrite.txt";
    write_req._Content = "hello world";
    // _Data also populated by fuse_adapter, _Content is what Node::processWriteFileRequest prioritizes
    write_req._Data = "hello world";

    Message write_res = node.processWriteFileRequest(write_req);
    EXPECT_EQ(write_res._Type, MessageType::WriteResponse);
    EXPECT_EQ(write_res._ErrorCode, 0);
    EXPECT_EQ(write_res._Size, write_req._Content.length());

    // Verify by reading back (tests if write actually happened)
    Message read_req;
    read_req._Type = MessageType::ReadFile;
    read_req._Filename = "newfiletowrite.txt";
    Message read_res = node.processReadFileRequest(read_req);
    EXPECT_EQ(read_res._Data, "hello world");
}


// Test processReplicateFileCommand
TEST_F(NodeTest, ProcessReplicateFileCommand_Success) {
    Node node(testNodeName, testPort); // Source node with real FileSystem
    // Create a file on the source node
    std::string rep_filename = "rep_me.txt";
    std::string rep_content = "replication data";
    node.processWriteFileRequest(Message{MessageType::WriteFile, rep_filename, rep_content});

    Message rep_cmd_msg;
    rep_cmd_msg._Type = MessageType::ReplicateFileCommand;
    rep_cmd_msg._Filename = rep_filename;
    rep_cmd_msg._NodeAddress = "127.0.0.1:9876"; // Target node address

    // Mock the client that processReplicateFileCommand will use to talk to the target node
    MockNetClient mock_target_node_client;
    // Configure mock_target_node_client (this is the client *passed into* processReplicateFileCommand)
    // It's already constructed by handleClient, CreateClientTCPSocket is called before processReplicateFileCommand

    EXPECT_CALL(mock_target_node_client, ConnectClientSocket())
        .WillOnce(testing::Return(true));

    Message expected_write_to_target;
    expected_write_to_target._Type = MessageType::WriteFile;
    expected_write_to_target._Filename = rep_filename;
    expected_write_to_target._Content = rep_content;
    expected_write_to_target._Size = rep_content.length();
    expected_write_to_target._Offset = 0;
    std::string expected_serialized_write = Message::Serialize(expected_write_to_target);

    EXPECT_CALL(mock_target_node_client, Send(testing::StrEq(expected_serialized_write.c_str())))
        .WillOnce(testing::Return(true));
    EXPECT_CALL(mock_target_node_client, Disconnect());

    Message response_to_metaserver = node.processReplicateFileCommand(rep_cmd_msg, mock_target_node_client);

    EXPECT_EQ(response_to_metaserver._Type, MessageType::ReplicateFileCommand);
    EXPECT_EQ(response_to_metaserver._ErrorCode, 0);
    EXPECT_EQ(response_to_metaserver._Filename, rep_filename);
}

TEST_F(NodeTest, ProcessReplicateFileCommand_ReadFileFails) {
    Node node(testNodeName, testPort);
    Message rep_cmd_msg;
    rep_cmd_msg._Type = MessageType::ReplicateFileCommand;
    rep_cmd_msg._Filename = "non_existent_rep_file.txt";
    rep_cmd_msg._NodeAddress = "127.0.0.1:9876";

    MockNetClient mock_target_node_client; // Not actually used if readFile fails first

    Message response_to_metaserver = node.processReplicateFileCommand(rep_cmd_msg, mock_target_node_client);
    EXPECT_EQ(response_to_metaserver._ErrorCode, ENOENT);
}

TEST_F(NodeTest, ProcessReplicateFileCommand_TargetConnectFails) {
    Node node(testNodeName, testPort);
    node.processWriteFileRequest(Message{MessageType::WriteFile, "rep_me_conn_fail.txt", "data"});

    Message rep_cmd_msg;
    rep_cmd_msg._Type = MessageType::ReplicateFileCommand;
    rep_cmd_msg._Filename = "rep_me_conn_fail.txt";
    rep_cmd_msg._NodeAddress = "127.0.0.1:9876";

    MockNetClient mock_target_node_client;
    EXPECT_CALL(mock_target_node_client, ConnectClientSocket())
        .WillOnce(testing::Return(false)); // Simulate connection failure
    // Send and Disconnect should not be called if Connect fails

    Message response_to_metaserver = node.processReplicateFileCommand(rep_cmd_msg, mock_target_node_client);
    EXPECT_EQ(response_to_metaserver._ErrorCode, EHOSTUNREACH);
}


TEST_F(NodeTest, ProcessDeleteFileRequest) {
    Node node(testNodeName, testPort);
    node.processWriteFileRequest(Message{MessageType::WriteFile, "file_to_delete.txt", "delete content"});

    Message del_req;
    del_req._Type = MessageType::DeleteFile;
    del_req._Filename = "file_to_delete.txt";

    Message del_res = node.processDeleteFileRequest(del_req);
    EXPECT_EQ(del_res._ErrorCode, 0);

    // Verify file is gone by trying to read it
    Message read_req;
    read_req._Type = MessageType::ReadFile;
    read_req._Filename = "file_to_delete.txt";
    Message read_res = node.processReadFileRequest(read_req);
    EXPECT_EQ(read_res._ErrorCode, ENOENT);
}
// Note: FileSystem is still real. To use MockFileSystem, Node needs constructor/setter injection.
// The test for ReplicateFileCommand correctly uses MockNetClient because it's passed as parameter.

#endif // TESTS_NODE_TESTS_CPP
