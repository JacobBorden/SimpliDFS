#include <gtest/gtest.h>
#include "filesystem.h"
#include "logger.h" // Add this include
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
