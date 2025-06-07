// Unit Tests for MetadataManager
#include "gtest/gtest.h"
#include "metaserver/metaserver.h"
#include "cluster/NodeHealthCache.h"
#include "utilities/blockio.hpp"
#include "utilities/server.h"
#include <thread>
#include "utilities/message.h"
#include <thread>

// Test Fixture for MetadataManager
class MetadataManagerTest : public ::testing::Test {
protected:
    MetadataManager metadataManager;

    struct DummyServer {
        Networking::Server server;
        std::thread th;
        std::vector<MessageType> received;
        DummyServer(int port, int expected)
            : server(port) {
            server.startListening();
            th = std::thread([this, expected](){
                for(int i=0;i<expected;i++) {
                    auto conn = server.Accept();
                    auto raw = server.Receive(conn);
                    if(!raw.empty()) {
                        Message msg = Message::Deserialize(std::string(raw.begin(), raw.end()));
                        received.push_back(msg._Type);
                    }
                    server.Send("", conn);
                    server.DisconnectClient(conn);
                }
            });
        }
        void stop() {
            if(th.joinable()) th.join();
            server.Shutdown();
        }
    };
};

// Test adding a new file
TEST_F(MetadataManagerTest, AddFile) {
    std::string filename = "testfile.txt";
    std::vector<std::string> nodes = {"Node1", "Node2", "Node3"};
    
    // Register nodes to make them available and alive
    metadataManager.registerNode("Node1", "127.0.0.1", 11001);
    metadataManager.registerNode("Node2", "127.0.0.1", 11002);
    metadataManager.registerNode("Node3", "127.0.0.1", 11003);

    DummyServer s1(11001,1), s2(11002,1), s3(11003,1);

    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644);
    ASSERT_EQ(add_result, 0); // 0 indicates success

    std::vector<std::string> storedNodes; 
    ASSERT_NO_THROW({
       storedNodes = metadataManager.getFileNodes(filename);
    });
    EXPECT_EQ(storedNodes, nodes);

    s1.stop(); s2.stop(); s3.stop();
}

// Test retrieving nodes for an existing file
TEST_F(MetadataManagerTest, GetFileNodes) {
    std::string filename = "testfile2.txt";
    std::vector<std::string> nodes = {"Node3", "Node4", "Node5"};

    metadataManager.registerNode("Node3", "127.0.0.1", 11003);
    metadataManager.registerNode("Node4", "127.0.0.1", 11004);
    metadataManager.registerNode("Node5", "127.0.0.1", 11005);

    DummyServer s1(11003,1), s2(11004,1), s3(11005,1);

    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // 0 indicates success

    std::vector<std::string> retrievedNodes;
    ASSERT_NO_THROW({
        retrievedNodes = metadataManager.getFileNodes(filename);
    });
    EXPECT_EQ(retrievedNodes, nodes);

    s1.stop(); s2.stop(); s3.stop();
}

// Test retrieving nodes for a non-existent file
TEST_F(MetadataManagerTest, GetFileNodesNonExistent) {
    std::string filename = "nonexistent.txt";
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);
}

// Test removing a file from metadata
TEST_F(MetadataManagerTest, RemoveFile) {
    std::string filename = "testfile3.txt";
    std::vector<std::string> nodes = {"Node5", "Node6", "Node7"};
    // Register nodes to make them available for addFile
    metadataManager.registerNode("Node5", "127.0.0.1", 11005);
    metadataManager.registerNode("Node6", "127.0.0.1", 11006);
    metadataManager.registerNode("Node7", "127.0.0.1", 11007);

    DummyServer s1(11005,2), s2(11006,2), s3(11007,2);
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // Ensure file was added successfully

    // bool removeFile(const std::string& filename)
    bool remove_result = metadataManager.removeFile(filename);
    ASSERT_TRUE(remove_result); // true indicates success
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);

    s1.stop(); s2.stop(); s3.stop();
}

// Test removing a non-existent file
TEST_F(MetadataManagerTest, RemoveNonExistentFile) {
    std::string filename = "nonexistentfile.txt";
    // bool removeFile(const std::string& filename)
    bool remove_result = metadataManager.removeFile(filename);
    ASSERT_FALSE(remove_result); // false indicates file not found or error
}

// Test printing metadata (no assertions, just ensuring no exceptions)
TEST_F(MetadataManagerTest, PrintMetadata) {
    std::string filename = "testfile4.txt";
    std::vector<std::string> nodes = {"Node7", "Node8", "Node9"};
    // Register nodes before adding file that uses them
    metadataManager.registerNode("Node7", "127.0.0.1", 11007);
    metadataManager.registerNode("Node8", "127.0.0.1", 11008);
    metadataManager.registerNode("Node9", "127.0.0.1", 11009);

    DummyServer s1(11007,1), s2(11008,1), s3(11009,1);
    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // 0 indicates success

    ASSERT_NO_THROW(metadataManager.printMetadata());

    s1.stop(); s2.stop(); s3.stop();
}

TEST_F(MetadataManagerTest, SaveLoadMetadataWithModesAndSizes) {
    const std::string filename = "persist_file.txt";
    const uint32_t mode = 0100644; // regular file with 0644 permissions
    const std::string node1 = "NodePersist1";
    const std::string node2 = "NodePersist2";
    const std::string node3 = "NodePersist3";

    metadataManager.registerNode(node1, "127.0.0.1", 1111);
    metadataManager.registerNode(node2, "127.0.0.1", 2222);
    metadataManager.registerNode(node3, "127.0.0.1", 3333);

    DummyServer s1(1111,4); // create + write + two replicate
    DummyServer s2(2222,2); // create + receive
    DummyServer s3(3333,2); // create + receive

    std::vector<std::string> nodes = {node1, node2, node3};
    ASSERT_EQ(metadataManager.addFile(filename, nodes, mode), 0);

    uint64_t written = 0;
    ASSERT_EQ(metadataManager.writeFileData(filename, 0, "hello world", written), 0);
    ASSERT_EQ(written, static_cast<uint64_t>(11));

    const std::string fm_path = "meta_save_test.dat";
    const std::string nr_path = "node_save_test.dat";
    metadataManager.saveMetadata(fm_path, nr_path);

    MetadataManager mm2;
    mm2.loadMetadata(fm_path, nr_path);

    ASSERT_TRUE(mm2.fileExists(filename));
    uint32_t loaded_mode, uid, gid; uint64_t loaded_size;
    ASSERT_EQ(mm2.getFileAttributes(filename, loaded_mode, uid, gid, loaded_size), 0);
    EXPECT_EQ(loaded_mode, mode);
    EXPECT_EQ(loaded_size, static_cast<uint64_t>(11));
    std::vector<std::string> loaded_nodes = mm2.getFileNodes(filename);
    EXPECT_EQ(loaded_nodes, nodes);

    BlockIO bio;
    std::vector<std::byte> bytes;
    for(char c : std::string("hello world")) bytes.push_back(std::byte(c));
    bio.ingest(bytes.data(), bytes.size());
    DigestResult dr = bio.finalize_hashed();
    EXPECT_EQ(mm2.getFileHash(filename), dr.cid);

    std::remove(fm_path.c_str());
    std::remove(nr_path.c_str());

    s1.stop(); s2.stop(); s3.stop();
}

TEST_F(MetadataManagerTest, InsufficientNodesAddFileFails) {
    metadataManager.registerNode("Node1", "127.0.0.1", 11001);
    metadataManager.registerNode("Node2", "127.0.0.1", 11002);
    std::vector<std::string> nodes = {"Node1", "Node2"};
    int res = metadataManager.addFile("insuff.txt", nodes, 0644);
    EXPECT_EQ(res, ERR_NO_REPLICA);
    EXPECT_FALSE(metadataManager.fileExists("insuff.txt"));
}

TEST_F(MetadataManagerTest, ZeroReplicaFails) {
    metadataManager.registerNode("A", "127.0.0.1", 13001);
    metadataManager.registerNode("B", "127.0.0.1", 13002);
    metadataManager.registerNode("C", "127.0.0.1", 13003);
    std::vector<std::string> nodes = {"A","B","C"};
    int res = metadataManager.addFile("zero.txt", nodes, 0644);
    EXPECT_EQ(res, ERR_NO_REPLICA);
    EXPECT_FALSE(metadataManager.fileExists("zero.txt"));
}

TEST_F(MetadataManagerTest, RollbackOnPartial) {
    metadataManager.registerNode("A", "127.0.0.1", 14001);
    metadataManager.registerNode("B", "127.0.0.1", 14002);
    metadataManager.registerNode("C", "127.0.0.1", 14003);
    DummyServer sa(14001,2); // create then delete
    std::vector<std::string> nodes = {"A","B","C"};
    int res = metadataManager.addFile("part.txt", nodes, 0644);
    EXPECT_EQ(res, ERR_INSUFFICIENT_REPLICA);
    EXPECT_FALSE(metadataManager.fileExists("part.txt"));
    sa.stop();
    ASSERT_EQ(sa.received.size(), 2u);
    EXPECT_EQ(sa.received[0], MessageType::WriteFile);
    EXPECT_EQ(sa.received[1], MessageType::DeleteFile);
}

TEST_F(MetadataManagerTest, NodeHealthCacheMarksFailures) {
    metadataManager.registerNode("A", "127.0.0.1", 15001);
    metadataManager.registerNode("B", "127.0.0.1", 15002);
    metadataManager.registerNode("C", "127.0.0.1", 15003);

    DummyServer sb(15002,2), sc(15003,2); // A has no server -> fails
    std::vector<std::string> nodes = {"A","B","C"};

    int res = metadataManager.addFile("health.txt", nodes, 0644);
    EXPECT_EQ(res, ERR_INSUFFICIENT_REPLICA);
    EXPECT_EQ(metadataManager.getNodeHealthState("A"), NodeState::SUSPECT);
    EXPECT_EQ(metadataManager.getNodeHealthState("B"), NodeState::ALIVE);

    sb.stop();
    sc.stop();
}

TEST(NodeHealthCache, FailureEscalation) {
    NodeHealthCache cache(2, 3, std::chrono::seconds(1));
    cache.recordFailure("X");
    cache.recordFailure("X");
    EXPECT_EQ(cache.state("X"), NodeState::DEAD);
}

TEST_F(MetadataManagerTest, ApplySnapshotDeltaAddsFile) {
    metadataManager.registerNode("A", "127.0.0.1", 16001);
    metadataManager.registerNode("B", "127.0.0.1", 16002);
    metadataManager.registerNode("C", "127.0.0.1", 16003);

    DummyServer sa(16001,3), sb(16002,2), sc(16003,2);

    std::string delta = "Added: delta.txt\n";
    bool changed = metadataManager.applySnapshotDelta("A", delta);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(metadataManager.fileExists("delta.txt"));

    sa.stop(); sb.stop(); sc.stop();
}
