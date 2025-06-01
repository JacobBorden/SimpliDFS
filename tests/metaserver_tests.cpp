// Unit Tests for MetadataManager
#include "gtest/gtest.h"
#include "metaserver/metaserver.h"
#include <vector>
#include <string>
#include <algorithm> // For std::sort, std::find
#include <cerrno>    // For error codes like ENOENT

// Test Fixture for MetadataManager
class MetadataManagerTest : public ::testing::Test {
protected:
    MetadataManager metadataManager;

    // Helper to register a few default alive nodes for tests that need them
    void RegisterDefaultNodes() {
        metadataManager.registerNode("Node1", "127.0.0.1", 1001);
        metadataManager.registerNode("Node2", "127.0.0.1", 1002);
        metadataManager.registerNode("Node3", "127.0.0.1", 1003);
        // Ensure they are marked alive by processing a heartbeat if necessary,
        // though registerNode itself marks them alive with current time.
        // For long tests, this might matter, but for most unit tests, initial registration is enough.
    }
};

// Test adding a new file
TEST_F(MetadataManagerTest, AddFile) {
    RegisterDefaultNodes();
    std::string filename = "testfile.txt";
    std::vector<std::string> preferredNodes = {"Node1", "Node2"}; // Test with preferred nodes
    
    int add_result = metadataManager.addFile(filename, preferredNodes, 0644);
    ASSERT_EQ(add_result, 0);

    std::vector<std::string> storedNodes; 
    ASSERT_NO_THROW({
       storedNodes = metadataManager.getFileNodes(filename);
    });
    // The actual nodes assigned might depend on DEFAULT_REPLICATION_FACTOR and node availability logic.
    // For this test, just check if some nodes were assigned if addFile succeeded.
    ASSERT_FALSE(storedNodes.empty());
}

// Test retrieving nodes for an existing file
TEST_F(MetadataManagerTest, GetFileNodes) {
    RegisterDefaultNodes();
    std::string filename = "testfile2.txt";
    std::vector<std::string> preferredNodes = {"Node1"};
    int add_result = metadataManager.addFile(filename, preferredNodes, 0644);
    ASSERT_EQ(add_result, 0);

    std::vector<std::string> retrievedNodes;
    ASSERT_NO_THROW({
        retrievedNodes = metadataManager.getFileNodes(filename);
    });
    ASSERT_FALSE(retrievedNodes.empty());
    // Could be more specific if node selection logic was deterministic and known for test
}

TEST_F(MetadataManagerTest, GetFileNodesNonExistent) {
    std::string filename = "nonexistent.txt";
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);
}

TEST_F(MetadataManagerTest, RemoveFile) {
    RegisterDefaultNodes();
    std::string filename = "testfile3.txt";
    std::vector<std::string> nodes = {}; // Let MM pick
    int add_result = metadataManager.addFile(filename, nodes, 0644);
    ASSERT_EQ(add_result, 0);

    bool remove_result = metadataManager.removeFile(filename);
    ASSERT_TRUE(remove_result);
    EXPECT_THROW(metadataManager.getFileNodes(filename), std::runtime_error);

    // Also check attributes are gone
    uint32_t mode, uid, gid;
    uint64_t size;
    ASSERT_EQ(metadataManager.getFileAttributes(filename, mode, uid, gid, size), ENOENT);
}

TEST_F(MetadataManagerTest, RemoveNonExistentFile) {
    std::string filename = "nonexistentfile.txt";
    bool remove_result = metadataManager.removeFile(filename);
    ASSERT_FALSE(remove_result);
}

TEST_F(MetadataManagerTest, PrintMetadata) {
    RegisterDefaultNodes();
    std::string filename = "testfile4.txt";
    std::vector<std::string> nodes = {};
    int add_result = metadataManager.addFile(filename, nodes, 0644);
    ASSERT_EQ(add_result, 0);
    ASSERT_NO_THROW(metadataManager.printMetadata());
}

// --- New Test Cases ---

TEST_F(MetadataManagerTest, GetFileAttributes_ExistingFile) {
    RegisterDefaultNodes();
    std::string filename = "attr_test.txt";
    uint32_t initial_mode = 0755;
    ASSERT_EQ(metadataManager.addFile(filename, {}, initial_mode), 0);

    uint32_t mode, uid, gid;
    uint64_t size;
    ASSERT_EQ(metadataManager.getFileAttributes(filename, mode, uid, gid, size), 0);
    ASSERT_EQ(mode, initial_mode);
    ASSERT_EQ(size, 0ULL); // Initial size
    ASSERT_EQ(uid, 0u);    // Default UID
    ASSERT_EQ(gid, 0u);    // Default GID
}

TEST_F(MetadataManagerTest, GetFileAttributes_NonExistentFile) {
    uint32_t mode, uid, gid;
    uint64_t size;
    ASSERT_EQ(metadataManager.getFileAttributes("nonexistent.txt", mode, uid, gid, size), ENOENT);
}

TEST_F(MetadataManagerTest, GetAllFileNames_Empty) {
    ASSERT_TRUE(metadataManager.getAllFileNames().empty());
}

TEST_F(MetadataManagerTest, GetAllFileNames_WithFiles) {
    RegisterDefaultNodes();
    ASSERT_EQ(metadataManager.addFile("file1.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.addFile("file2.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.addFile("another.log", {}, 0644), 0);

    std::vector<std::string> names = metadataManager.getAllFileNames();
    ASSERT_EQ(names.size(), 3);
    // Sort for consistent comparison
    std::sort(names.begin(), names.end());
    ASSERT_EQ(names[0], "another.log");
    ASSERT_EQ(names[1], "file1.txt");
    ASSERT_EQ(names[2], "file2.txt");
}

TEST_F(MetadataManagerTest, CheckAccess_ExistingFile) {
    RegisterDefaultNodes();
    ASSERT_EQ(metadataManager.addFile("access_test.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.checkAccess("access_test.txt", 0), 0); // Mask not used yet
}

TEST_F(MetadataManagerTest, CheckAccess_NonExistentFile) {
    ASSERT_EQ(metadataManager.checkAccess("nonexistent.txt", 0), ENOENT);
}

TEST_F(MetadataManagerTest, OpenFile_ExistingFile) {
    RegisterDefaultNodes();
    ASSERT_EQ(metadataManager.addFile("open_test.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.openFile("open_test.txt", 0), 0); // Flags not used yet
}

TEST_F(MetadataManagerTest, OpenFile_NonExistentFile) {
    ASSERT_EQ(metadataManager.openFile("nonexistent.txt", 0), ENOENT);
}

TEST_F(MetadataManagerTest, RenameFileEntry_Success) {
    RegisterDefaultNodes();
    std::string old_name = "old_rename.txt";
    std::string new_name = "new_rename.txt";
    uint32_t old_mode = 0777;
    ASSERT_EQ(metadataManager.addFile(old_name, {}, old_mode), 0);

    // Store nodes assigned to old_name to compare later (if needed, though not strictly necessary for this test)
    // std::vector<std::string> old_nodes = metadataManager.getFileNodes(old_name);

    ASSERT_EQ(metadataManager.renameFileEntry(old_name, new_name), 0);

    uint32_t mode, uid, gid;
    uint64_t size;
    // Verify old_name is gone
    ASSERT_EQ(metadataManager.getFileAttributes(old_name, mode, uid, gid, size), ENOENT);
    // Verify new_name exists with old_name's attributes
    ASSERT_EQ(metadataManager.getFileAttributes(new_name, mode, uid, gid, size), 0);
    ASSERT_EQ(mode, old_mode);
    ASSERT_EQ(size, 0ULL); // Size should be preserved (0 in this case)
    // std::vector<std::string> new_nodes = metadataManager.getFileNodes(new_name);
    // ASSERT_EQ(new_nodes, old_nodes); // Node assignment should also be preserved
}

TEST_F(MetadataManagerTest, RenameFileEntry_ToExisting) {
    RegisterDefaultNodes();
    ASSERT_EQ(metadataManager.addFile("file_a.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.addFile("file_b.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.renameFileEntry("file_a.txt", "file_b.txt"), EEXIST);
}

TEST_F(MetadataManagerTest, RenameFileEntry_NonExistentSource) {
    RegisterDefaultNodes(); // Ensure it's not failing due to no nodes for target
    ASSERT_EQ(metadataManager.renameFileEntry("non_existent_old.txt", "any_new_name.txt"), ENOENT);
}

TEST_F(MetadataManagerTest, GetFileStatx_ExistingFile) {
    RegisterDefaultNodes();
    std::string filename = "statx_test.txt";
    uint32_t initial_mode = 0755;
    ASSERT_EQ(metadataManager.addFile(filename, {}, initial_mode), 0);

    uint32_t mode, uid, gid;
    uint64_t size;
    std::string timestamps_data; // Ignored for now as per implementation
    ASSERT_EQ(metadataManager.getFileStatx(filename, mode, size, uid, gid, timestamps_data), 0);
    ASSERT_EQ(mode, initial_mode);
    ASSERT_EQ(size, 0ULL);
    ASSERT_EQ(uid, 0u);
    ASSERT_EQ(gid, 0u);
    ASSERT_TRUE(timestamps_data.empty()); // Expect empty as per current getFileStatx
}

TEST_F(MetadataManagerTest, GetFileStatx_NonExistentFile) {
    uint32_t mode, uid, gid;
    uint64_t size;
    std::string timestamps_data;
    ASSERT_EQ(metadataManager.getFileStatx("nonexistent_statx.txt", mode, size, uid, gid, timestamps_data), ENOENT);
}

TEST_F(MetadataManagerTest, UpdateFileTimestamps_ExistingFile) {
    RegisterDefaultNodes();
    ASSERT_EQ(metadataManager.addFile("utimens_test.txt", {}, 0644), 0);
    ASSERT_EQ(metadataManager.updateFileTimestamps("utimens_test.txt", "dummy_timestamp_data"), 0);
    // Current implementation only logs, so we just check for success.
}

TEST_F(MetadataManagerTest, UpdateFileTimestamps_NonExistentFile) {
    ASSERT_EQ(metadataManager.updateFileTimestamps("nonexistent_utimens.txt", "dummy_data"), ENOENT);
}

// Test case for addFile when no nodes are registered/alive
TEST_F(MetadataManagerTest, AddFile_NoNodes) {
    // No nodes registered
    std::string filename = "no_nodes_file.txt";
    ASSERT_EQ(metadataManager.addFile(filename, {}, 0644), ENOSPC);
}
