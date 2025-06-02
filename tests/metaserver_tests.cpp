// Unit Tests for MetadataManager
#include "gtest/gtest.h"
#include "metaserver/metaserver.h"

// Test Fixture for MetadataManager
class MetadataManagerTest : public ::testing::Test {
protected:
    MetadataManager metadataManager;
};

// Test adding a new file
TEST_F(MetadataManagerTest, AddFile) {
    std::string filename = "testfile.txt";
    std::vector<std::string> nodes = {"Node1", "Node2"};
    
    // Register nodes to make them available and alive
    metadataManager.registerNode("Node1", "localhost", 1001);
    metadataManager.registerNode("Node2", "localhost", 1002);

    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // 0 indicates success

    std::vector<std::string> storedNodes; 
    ASSERT_NO_THROW({
       storedNodes = metadataManager.getFileNodes(filename);
    });
    EXPECT_EQ(storedNodes, nodes);
}

// Test retrieving nodes for an existing file
TEST_F(MetadataManagerTest, GetFileNodes) {
    std::string filename = "testfile2.txt";
    std::vector<std::string> nodes = {"Node3", "Node4"};

    // Register nodes
    metadataManager.registerNode("Node3", "localhost", 1003);
    metadataManager.registerNode("Node4", "localhost", 1004);

    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // 0 indicates success

    std::vector<std::string> retrievedNodes;
    ASSERT_NO_THROW({
        retrievedNodes = metadataManager.getFileNodes(filename);
    });
    EXPECT_EQ(retrievedNodes, nodes);
}

// Test retrieving nodes for a non-existent file
TEST_F(MetadataManagerTest, GetFileNodesNonExistent) {
    std::string filename = "nonexistent.txt";
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);
}

// Test removing a file from metadata
TEST_F(MetadataManagerTest, RemoveFile) {
    std::string filename = "testfile3.txt";
    std::vector<std::string> nodes = {"Node5", "Node6"};
    // Register nodes to make them available for addFile
    metadataManager.registerNode("Node5", "localhost", 1005);
    metadataManager.registerNode("Node6", "localhost", 1006);
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // Ensure file was added successfully

    // bool removeFile(const std::string& filename)
    bool remove_result = metadataManager.removeFile(filename);
    ASSERT_TRUE(remove_result); // true indicates success
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);
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
    std::vector<std::string> nodes = {"Node7", "Node8"};
    // Register nodes before adding file that uses them
    metadataManager.registerNode("Node7", "localhost", 1007);
    metadataManager.registerNode("Node8", "localhost", 1008);
    // int addFile(const std::string& filename, const std::vector<std::string>& preferredNodes, uint32_t mode)
    int add_result = metadataManager.addFile(filename, nodes, 0644); // Using a common octal mode
    ASSERT_EQ(add_result, 0); // 0 indicates success

    ASSERT_NO_THROW(metadataManager.printMetadata());
}

// Test for updateFileSizePostWrite
TEST_F(MetadataManagerTest, UpdateFileSizePostWrite_NewFile) {
    std::string filename = "sizeUpdateFile.txt";
    // Register a node and add the file so it exists in fileMetadata
    metadataManager.registerNode("NodeS1", "localhost", 1009);
    std::vector<std::string> nodes = {"NodeS1"};
    ASSERT_EQ(metadataManager.addFile(filename, nodes, 0644), 0);

    // Initial size is 0. Write 100 bytes at offset 0.
    int result = metadataManager.updateFileSizePostWrite(filename, 0, 100);
    ASSERT_EQ(result, 0);

    uint32_t mode, uid, gid;
    uint64_t size;
    metadataManager.getFileAttributes(filename, mode, uid, gid, size);
    EXPECT_EQ(size, 100);

    // Write another 50 bytes, appending
    result = metadataManager.updateFileSizePostWrite(filename, 100, 50);
    ASSERT_EQ(result, 0);
    metadataManager.getFileAttributes(filename, mode, uid, gid, size);
    EXPECT_EQ(size, 150);

    // Overwrite first 50 bytes - size should not change if new_potential_size is not greater
    result = metadataManager.updateFileSizePostWrite(filename, 0, 50);
    ASSERT_EQ(result, 0);
    metadataManager.getFileAttributes(filename, mode, uid, gid, size);
    EXPECT_EQ(size, 150); // Still 150 because 0 + 50 is not > 150

     // Write past current EOF
    result = metadataManager.updateFileSizePostWrite(filename, 200, 50);
    ASSERT_EQ(result, 0);
    metadataManager.getFileAttributes(filename, mode, uid, gid, size);
    EXPECT_EQ(size, 250); // 200 + 50
}

TEST_F(MetadataManagerTest, UpdateFileSizePostWrite_FileNotFound) {
    int result = metadataManager.updateFileSizePostWrite("nonExistentForSize.txt", 0, 100);
    ASSERT_EQ(result, ENOENT);
}


// Placeholder for checkForDeadNodes tests
// Testing checkForDeadNodes fully requires mocking Networking::Client and time.
// For now, this is a conceptual placeholder.
TEST_F(MetadataManagerTest, CheckForDeadNodes_Conceptual) {
    // 1. Register a few nodes.
    // 2. Add a file associated with these nodes.
    // 3. Simulate time passing so a node times out (e.g., by directly manipulating lastHeartbeat if possible, or needing a time mock).
    // 4. Call checkForDeadNodes.
    // 5. Verify:
    //    - Dead node is marked !isAlive.
    //    - Logs indicate attempts to re-replicate (using mock client expectations if Networking::Client was mockable/injectable).
    //    - File metadata is updated with a new node.
    // This test would be complex due to direct Networking::Client instantiation in checkForDeadNodes.
    // For now, we just assert it doesn't crash and rely on INFO/ERROR logs from manual testing.
    // ASSERT_NO_THROW(metadataManager.checkForDeadNodes()); // Original placeholder

    // Setup
    const std::string deadNodeID = "DeadNode";
    const std::string liveNodeID1 = "LiveNode1";
    const std::string liveNodeID2 = "LiveNode2"; // This will be the new replica node
    const std::string liveNodeID3 = "LiveNode3"; // Another live node, source for replica

    metadataManager.registerNode(deadNodeID, "10.0.0.1", 1001);
    metadataManager.registerNode(liveNodeID1, "10.0.0.2", 1002);
    metadataManager.registerNode(liveNodeID2, "10.0.0.3", 1003);
    metadataManager.registerNode(liveNodeID3, "10.0.0.4", 1004);


    std::string testFile1 = "file1.txt";
    std::vector<std::string> file1Nodes = {deadNodeID, liveNodeID3}; // deadNode has a replica
    metadataManager.addFile(testFile1, file1Nodes, 0644);

    std::string testFile2 = "file2.txt";
    std::vector<std::string> file2Nodes = {liveNodeID1, liveNodeID3}; // deadNode does not have this
    metadataManager.addFile(testFile2, file2Nodes, 0644);

    // Simulate DeadNode timeout: For testing, we need to make lastHeartbeat very old.
    // This requires either a) a way to mock time, b) a way to set lastHeartbeat directly,
    // or c) waiting for NODE_TIMEOUT_SECONDS. Option (c) is not suitable for unit tests.
    // Let's assume there's a way to modify NodeInfo for tests or a helper.
    // If not, this test can only check that no crash occurs.
    // For now, we can't directly manipulate lastHeartbeat as it's private.
    // So, this test primarily checks that the logic attempts to run without crashing
    // and we'd rely on logs for network attempts.
    // To make it more testable, MetadataManager could offer a way to "force_timeout" a node for testing.

    // We expect the internal try/catch blocks in checkForDeadNodes to handle network errors.
    // The main verifiable outcome without deeper mocks/time control is that the node *would be*
    // identified as dead if time was proper, and the function completes.
    // We can verify that if a node *is* marked dead, the rest of the logic tries to run.

    // To test the metadata update part, we could manually mark a node as not alive
    // and then see if replication task selection works.
    // This is a bit indirect.

    // Let's test the state after calling it once (nodes are fresh, no one is dead yet by timeout)
    ASSERT_NO_THROW(metadataManager.checkForDeadNodes());
    NodeInfo deadNodeInfo_after1 = metadataManager.getNodeInfo(deadNodeID);
    ASSERT_TRUE(deadNodeInfo_after1.isAlive); // Should still be alive

    // If we could manipulate time or lastHeartbeat to be > NODE_TIMEOUT_SECONDS ago for deadNodeID
    // Then call metadataManager.checkForDeadNodes() again.
    // Then we would expect:
    // NodeInfo deadNodeInfo_after2 = metadataManager.getNodeInfo(deadNodeID);
    // EXPECT_FALSE(deadNodeInfo_after2.isAlive);
    // std::vector<std::string> newFile1Nodes = metadataManager.getFileNodes(testFile1);
    // EXPECT_NE(std::find(newFile1Nodes.begin(), newFile1Nodes.end(), deadNodeID), newFile1Nodes.end()); // Should be removed
    // EXPECT_NE(std::find(newFile1Nodes.begin(), newFile1Nodes.end(), liveNodeID2), newFile1Nodes.end()); // liveNodeID2 should be added

    // This test remains conceptual due to difficulties in mocking time or direct client instantiation.
    // The primary check is that it runs without throwing an unhandled exception.
    // Log messages would need to be inspected in a more integrated test environment.
}


// Tests for logic related to HandleClientConnection cases
TEST_F(MetadataManagerTest, GetNodeAddressAndInfo) {
    metadataManager.registerNode("TestNode1", "127.0.0.1", 1234);
    ASSERT_EQ(metadataManager.getNodeAddress("TestNode1"), "127.0.0.1:1234");

    NodeInfo info = metadataManager.getNodeInfo("TestNode1");
    ASSERT_EQ(info.nodeAddress, "127.0.0.1:1234");
    ASSERT_TRUE(info.isAlive);

    ASSERT_THROW(metadataManager.getNodeAddress("NonExistentNode"), std::runtime_error);
    ASSERT_THROW(metadataManager.getNodeInfo("NonExistentNode"), std::runtime_error);
}

// This tests the logic that HandleClientConnection's GetFileNodeLocations case would use
TEST_F(MetadataManagerTest, GetFileNodeLocations_Logic) {
    std::string filename = "fileForLoc.txt";
    metadataManager.registerNode("N1", "1.1.1.1", 111);
    metadataManager.registerNode("N2", "2.2.2.2", 222);
    std::vector<std::string> node_ids = {"N1", "N2"};
    metadataManager.addFile(filename, node_ids, 0644);

    std::vector<std::string> retrieved_node_ids = metadataManager.getFileNodes(filename);
    ASSERT_EQ(retrieved_node_ids.size(), 2);

    std::string addresses;
    for(size_t i = 0; i < retrieved_node_ids.size(); ++i) {
        addresses += metadataManager.getNodeAddress(retrieved_node_ids[i]);
        if (i < retrieved_node_ids.size() - 1) addresses += ",";
    }
    // Order might vary depending on map iteration, so check for presence
    EXPECT_TRUE(addresses.find("1.1.1.1:111") != std::string::npos);
    EXPECT_TRUE(addresses.find("2.2.2.2:222") != std::string::npos);
}

// This tests logic for HandleClientConnection's PrepareWriteOperation (finding primary node)
TEST_F(MetadataManagerTest, PrepareWriteOperation_Logic_FindPrimary) {
    std::string filename = "fileForWritePrep.txt";
    metadataManager.registerNode("PNode1", "1.2.3.4", 567);
    metadataManager.registerNode("PNode2", "5.6.7.8", 890);
    std::vector<std::string> node_ids_prep = {"PNode1", "PNode2"};
    metadataManager.addFile(filename, node_ids_prep, 0644);

    std::vector<std::string> retrieved_node_ids_prep = metadataManager.getFileNodes(filename);
    ASSERT_FALSE(retrieved_node_ids_prep.empty());

    std::string primary_node_addr;
    bool primary_found = false;
    for(const auto& id : retrieved_node_ids_prep) {
        NodeInfo info = metadataManager.getNodeInfo(id);
        if(info.isAlive) { // All should be alive initially
            primary_node_addr = info.nodeAddress;
            primary_found = true;
            break;
        }
    }
    ASSERT_TRUE(primary_found);
    // Check if it's one of the expected addresses
    bool is_valid_addr = (primary_node_addr == "1.2.3.4:567" || primary_node_addr == "5.6.7.8:890");
    ASSERT_TRUE(is_valid_addr);
}
