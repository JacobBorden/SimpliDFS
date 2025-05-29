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

    ASSERT_NO_THROW(metadataManager.addFile(filename, nodes));

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

    metadataManager.addFile(filename, nodes); 

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
    metadataManager.addFile(filename, nodes);

    ASSERT_NO_THROW(metadataManager.removeFile(filename));
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);
}

// Test removing a non-existent file
TEST_F(MetadataManagerTest, RemoveNonExistentFile) {
    std::string filename = "nonexistentfile.txt";
    ASSERT_NO_THROW(metadataManager.removeFile(filename));
}

// Test printing metadata (no assertions, just ensuring no exceptions)
TEST_F(MetadataManagerTest, PrintMetadata) {
    std::string filename = "testfile4.txt";
    std::vector<std::string> nodes = {"Node7", "Node8"};
    // Register nodes before adding file that uses them
    metadataManager.registerNode("Node7", "localhost", 1007);
    metadataManager.registerNode("Node8", "localhost", 1008);
    metadataManager.addFile(filename, nodes);

    ASSERT_NO_THROW(metadataManager.printMetadata());
}
