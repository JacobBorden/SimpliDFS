#include <gtest/gtest.h>
#include "utilities/filesystem.h"
#include "utilities/logger.h" // Add this include
#include <cstdio>   // For std::remove
#include <string>   // For std::to_string in TearDown

class FileSystemTestFix : public ::testing::Test {
protected:
    void SetUp() override {
        // It's possible init might throw if there's a fundamental issue,
        // though tests generally expect it to succeed.
        try {
            Logger::init("filesystem_tests.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            // Optionally handle or log this, but for tests, an ASSERT_NO_THROW in SetUp
            // might be too much if the focus is not logger itself.
            // For now, let it throw if it must.
        }
    }
    void TearDown() override {
        // Attempt to init logger to a dummy file to release handle on test log file
        // This is a workaround for singleton logger file handles persisting.
        try {
            Logger::init("dummy_fs_cleanup.log", LogLevel::DEBUG);
            std::remove("dummy_fs_cleanup.log"); // Clean up the dummy log itself
            // Clean up potential rotated dummy log
            std::remove("dummy_fs_cleanup.log.1"); 
        } catch (const std::runtime_error& e) { /* Logger might not have been initted if SetUp failed */ }
        
        std::remove("filesystem_tests.log");
        // Clean up potential rotated files if any were created
        for (int i = 1; i <= 5; ++i) { // Check a few potential backup numbers
            std::remove(("filesystem_tests.log." + std::to_string(i)).c_str());
        }
    }
};

TEST_F(FileSystemTestFix, createFile)
{
	FileSystem fs;
	bool first = fs.createFile("Test");
	bool second = fs.createFile("Test");
	
	ASSERT_EQ(first, true);
	ASSERT_EQ(second, false);
}

TEST_F(FileSystemTestFix, writeFile)
{
	FileSystem fs;
	fs.createFile("Test"); // Depends on createFile working
	bool first = fs.writeFile("Test", "Test");
	bool second = fs.writeFile("Test2", "Test"); // Writing to a non-existent file

	ASSERT_EQ(first,true);
	ASSERT_EQ(second,false);

}


TEST_F(FileSystemTestFix, readFile)
{
	FileSystem fs;
	fs.createFile("Test"); // Depends on createFile
	fs.writeFile("Test", "Read Test"); // Depends on writeFile
	std::string data = fs.readFile("Test");
    std::string non_existent_data = fs.readFile("NonExistentFile");

	ASSERT_EQ(data, "Read Test");
	ASSERT_EQ(non_existent_data, ""); // Expecting empty string for non-existent file
	 	
}

// It might be good to add a deleteFile test if it's part of FileSystem functionality.
// Assuming FileSystem::deleteFile exists based on previous tasks.
TEST_F(FileSystemTestFix, deleteFile)
{
    FileSystem fs;
    fs.createFile("ToDelete.txt");
    ASSERT_TRUE(fs.deleteFile("ToDelete.txt"));
    ASSERT_FALSE(fs.deleteFile("NonExistent.txt")); // Test deleting non-existent file
    ASSERT_EQ(fs.readFile("ToDelete.txt"), ""); // Verify it's gone (or returns empty)
}

TEST_F(FileSystemTestFix, extendedAttributes)
{
    FileSystem fs;
    const std::string filename = "xattr_file.txt";
    const std::string non_existent_filename = "non_existent_xattr_file.txt";
    const std::string attr_name = "user.cid";
    const std::string attr_value1 = "test_cid_value_123";
    const std::string attr_value2 = "test_cid_value_456";

    // Pre-condition: Create a file
    ASSERT_TRUE(fs.createFile(filename));

    // Test 1: Set and Get an extended attribute
    fs.setXattr(filename, attr_name, attr_value1);
    // No explicit return value for setXattr to check, success is verified by getXattr
    std::string retrieved_value = fs.getXattr(filename, attr_name);
    ASSERT_EQ(retrieved_value, attr_value1);

    // Test 2: Get a non-existent attribute for an existing file
    std::string non_existent_attr = fs.getXattr(filename, "user.nonexistentattr");
    ASSERT_EQ(non_existent_attr, "");

    // Test 3: Get an attribute for a non-existent file
    std::string non_existent_file_attr = fs.getXattr(non_existent_filename, attr_name);
    ASSERT_EQ(non_existent_file_attr, "");

    // Test 4: Set an attribute on a non-existent file (should be a no-op or log error)
    fs.setXattr(non_existent_filename, attr_name, "value_for_non_existent_file");
    std::string check_attr_non_existent_file = fs.getXattr(non_existent_filename, attr_name);
    ASSERT_EQ(check_attr_non_existent_file, ""); // Expecting it not to be set

    // Test 5: Overwrite an existing attribute
    fs.setXattr(filename, attr_name, attr_value2);
    std::string updated_value = fs.getXattr(filename, attr_name);
    ASSERT_EQ(updated_value, attr_value2);

    // Test 6: Get attribute after file deletion
    // First, set an attribute, then delete the file, then try to get.
    const std::string file_to_delete = "file_for_xattr_deletion_test.txt";
    ASSERT_TRUE(fs.createFile(file_to_delete));
    fs.setXattr(file_to_delete, attr_name, "cid_before_delete");
    ASSERT_TRUE(fs.deleteFile(file_to_delete)); // Delete the file
    std::string attr_after_delete = fs.getXattr(file_to_delete, attr_name);
    ASSERT_EQ(attr_after_delete, ""); // Should be empty as file (and its xattrs) are gone
}
