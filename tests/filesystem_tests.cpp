#include "utilities/blockio.hpp" // For crypto constants
#include "utilities/filesystem.h"
#include "utilities/key_manager.hpp"
#include "utilities/logger.h"
#include <algorithm>
#include <cstddef> // For std::byte
#include <cstdio>  // For std::remove
#include <gtest/gtest.h>
#include <stdexcept> // For std::runtime_error
#include <string>    // For std::to_string in TearDown
#include <sys/stat.h>
#include <vector>

// Helper to convert vector<unsigned char> to string (for comparing nonce
// storage) Nonces can contain nulls, so direct string construction is fine.
std::string uchar_vec_to_string(const std::vector<unsigned char> &vec) {
  return std::string(reinterpret_cast<const char *>(vec.data()), vec.size());
}
// Helper to convert string to vector<unsigned char>
std::vector<unsigned char> string_to_uchar_vec(const std::string &str) {
  std::vector<unsigned char> vec(str.length());
  std::transform(str.begin(), str.end(), vec.begin(),
                 [](char c) { return static_cast<unsigned char>(c); });
  return vec;
}

class FileSystemTestFix : public ::testing::Test {
protected:
  FileSystem fs; // Use a single FileSystem instance for each test case

  void SetUp() override {
    try {
      // Using a unique log file per test run might be better if tests run in
      // parallel or if log content is critical for debugging specific tests.
      Logger::init("filesystem_tests.log", LogLevel::DEBUG);
      simplidfs::KeyManager::getInstance().initialize();
    } catch (const std::exception &e) {
      // No action needed if logger init fails, tests might still run
    }
  }

  void TearDown() override {
    try {
      Logger::init("dummy_fs_cleanup.log",
                   LogLevel::DEBUG); // Release main log file
      std::remove("dummy_fs_cleanup.log");
      std::remove("dummy_fs_cleanup.log.1");
    } catch (const std::runtime_error &e) { /* ignore */
    }

    std::remove("filesystem_tests.log");
    for (int i = 1; i <= 5; ++i) { // Clean up potential rotated files
      std::remove(("filesystem_tests.log." + std::to_string(i)).c_str());
    }
  }
};

TEST_F(FileSystemTestFix, CreateFile) {
  ASSERT_TRUE(fs.createFile("Test.txt"));
  EXPECT_EQ(fs.readFile("Test.txt"), ""); // Newly created file should be empty
  // Xattrs should not exist yet for a file that hasn't had content written via
  // new pipeline
  EXPECT_TRUE(fs.getXattr("Test.txt", "user.cid").empty());
  ASSERT_FALSE(fs.createFile("Test.txt")); // Already exists
}

TEST_F(FileSystemTestFix, WriteAndReadFile_Basic) {
  const std::string filename = "TestWriteRead.txt";
  const std::string content =
      "This is some test content for the file system pipeline.";

  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, content));

  // Verify xattrs (basic checks)
  std::string cid = fs.getXattr(filename, "user.cid");
  std::string nonce_str = fs.getXattr(filename, "user.nonce");
  std::string original_size_str = fs.getXattr(filename, "user.original_size");
  std::string encrypted_size_str = fs.getXattr(filename, "user.encrypted_size");

  EXPECT_FALSE(cid.empty());
  EXPECT_FALSE(nonce_str.empty());
  EXPECT_EQ(nonce_str.length(),
            crypto_aead_xchacha20poly1305_ietf_NPUBBYTES); // Nonce string raw
                                                           // byte storage
  EXPECT_EQ(original_size_str, std::to_string(content.length()));
  EXPECT_FALSE(encrypted_size_str.empty());
  if (!encrypted_size_str.empty()) {
    EXPECT_EQ(std::stoul(encrypted_size_str),
              content.length() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
  }

  std::string read_content = fs.readFile(filename);
  EXPECT_EQ(read_content, content);
}

TEST_F(FileSystemTestFix, WriteAndReadFile_EmptyContent) {
  const std::string filename = "EmptyFile.txt";
  const std::string content = "";

  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, content));

  EXPECT_FALSE(
      fs.getXattr(filename, "user.cid").empty()); // CID of empty string
  std::string nonce_str = fs.getXattr(filename, "user.nonce");
  EXPECT_FALSE(nonce_str.empty());
  EXPECT_EQ(nonce_str.length(), crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
  EXPECT_EQ(fs.getXattr(filename, "user.original_size"), "0");
  EXPECT_EQ(fs.getXattr(filename, "user.encrypted_size"),
            std::to_string(crypto_aead_xchacha20poly1305_ietf_ABYTES));

  std::string read_content = fs.readFile(filename);
  EXPECT_EQ(read_content, content);
}

TEST_F(FileSystemTestFix, WriteToNonExistentFile) {
  ASSERT_FALSE(fs.writeFile("NonExistentWrite.txt", "content"));
}

TEST_F(FileSystemTestFix, ReadNonExistentFile) {
  EXPECT_EQ(fs.readFile("NonExistentRead.txt"), "");
}

TEST_F(FileSystemTestFix, OverwriteExistingFile) {
  const std::string filename = "Overwrite.txt";
  const std::string content1 = "Initial content.";
  const std::string content2 =
      "Overwritten content which is much longer to ensure new processing.";

  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, content1));
  std::string cid1 = fs.getXattr(filename, "user.cid");
  std::string nonce1 = fs.getXattr(filename, "user.nonce");

  ASSERT_TRUE(fs.writeFile(filename, content2));
  std::string cid2 = fs.getXattr(filename, "user.cid");
  std::string nonce2 = fs.getXattr(filename, "user.nonce");

  EXPECT_NE(cid1, cid2);
  EXPECT_NE(nonce1, nonce2); // Nonce should change with each encryption

  std::string read_content = fs.readFile(filename);
  EXPECT_EQ(read_content, content2);
  EXPECT_EQ(fs.getXattr(filename, "user.original_size"),
            std::to_string(content2.length()));
}

TEST_F(FileSystemTestFix, DeleteFile) {
  const std::string filename = "ToDelete.txt";
  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, "some data to ensure xattrs are created"));
  // Check an xattr exists before delete
  ASSERT_FALSE(fs.getXattr(filename, "user.cid").empty());

  ASSERT_TRUE(fs.deleteFile(filename));
  EXPECT_EQ(fs.readFile(filename), "");
  EXPECT_TRUE(
      fs.getXattr(filename, "user.cid").empty()); // Check if xattrs are gone

  ASSERT_FALSE(fs.deleteFile("NonExistentDelete.txt"));
}

TEST_F(FileSystemTestFix, RenameFile) {
  const std::string old_filename = "OldName.txt";
  const std::string new_filename = "NewName.txt";
  const std::string content = "Content for rename test.";

  ASSERT_TRUE(fs.createFile(old_filename));
  ASSERT_TRUE(fs.writeFile(old_filename, content));
  std::string old_cid = fs.getXattr(old_filename, "user.cid");
  std::string old_nonce = fs.getXattr(old_filename, "user.nonce");
  std::string old_orig_size = fs.getXattr(old_filename, "user.original_size");
  std::string old_enc_size = fs.getXattr(old_filename, "user.encrypted_size");

  ASSERT_FALSE(old_cid.empty());

  ASSERT_TRUE(fs.renameFile(old_filename, new_filename));

  // Check old file and its xattrs are gone
  EXPECT_EQ(fs.readFile(old_filename), "");
  EXPECT_TRUE(fs.getXattr(old_filename, "user.cid").empty());
  EXPECT_TRUE(fs.getXattr(old_filename, "user.nonce").empty());

  // Check new file has content and all xattrs
  EXPECT_EQ(fs.readFile(new_filename), content);
  EXPECT_EQ(fs.getXattr(new_filename, "user.cid"), old_cid);
  EXPECT_EQ(fs.getXattr(new_filename, "user.nonce"), old_nonce);
  EXPECT_EQ(fs.getXattr(new_filename, "user.original_size"), old_orig_size);
  EXPECT_EQ(fs.getXattr(new_filename, "user.encrypted_size"), old_enc_size);

  // Test renaming non-existent file
  ASSERT_FALSE(fs.renameFile("NonExistentOld.txt", "anyNewName.txt"));
  // Test renaming to an existing file name (should fail)
  ASSERT_TRUE(fs.createFile("ExistingTarget.txt"));
  ASSERT_FALSE(fs.renameFile(new_filename, "ExistingTarget.txt"));
}

TEST_F(FileSystemTestFix, ReadFile_TamperedNonce) {
  const std::string filename = "TamperNonce.txt";
  const std::string content = "Sensitive data for nonce tampering test.";

  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, content));

  std::string original_nonce_str = fs.getXattr(filename, "user.nonce");
  ASSERT_FALSE(original_nonce_str.empty());

  // Tamper with the nonce: convert to uchar vec, modify, convert back to string
  // for setXattr
  std::vector<unsigned char> tampered_nonce_vec =
      string_to_uchar_vec(original_nonce_str);
  if (!tampered_nonce_vec.empty()) {
    tampered_nonce_vec[0]++;
  } else {
    FAIL() << "Nonce is empty, cannot tamper.";
  }
  fs.setXattr(filename, "user.nonce", uchar_vec_to_string(tampered_nonce_vec));

  // Expect readFile to fail (returns empty string due to internal catch block)
  EXPECT_EQ(fs.readFile(filename), "");
  // Ideally, we'd check for a specific exception type or error log if
  // FileSystem re-threw.
}

TEST_F(FileSystemTestFix, ReadFile_TamperedCID_Simulated) {
  const std::string filename = "TamperCID.txt";
  const std::string content = "Data with CID to be tampered for test.";

  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, content));

  std::string original_cid = fs.getXattr(filename, "user.cid");
  ASSERT_FALSE(original_cid.empty());

  fs.setXattr(filename, "user.cid",
              "a_completely_fake_and_tampered_cid_value_that_will_not_match");

  EXPECT_EQ(fs.readFile(filename),
            ""); // Expect empty string due to CID mismatch exception
}

TEST_F(FileSystemTestFix, SnapshotCreateCheckout) {
  const std::string filename = "snap.txt";
  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, "good"));
  ASSERT_TRUE(fs.snapshotCreate("s1"));

  ASSERT_TRUE(fs.writeFile(filename, "bad"));
  ASSERT_TRUE(fs.snapshotCheckout("s1"));
  EXPECT_EQ(fs.readFile(filename), "good");
}

TEST_F(FileSystemTestFix, SnapshotDiff) {
  const std::string filename = "diff.txt";
  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, "a"));
  ASSERT_TRUE(fs.snapshotCreate("snap"));

  ASSERT_TRUE(fs.writeFile(filename, "b"));
  auto diff = fs.snapshotDiff("snap");
  ASSERT_FALSE(diff.empty());
  EXPECT_EQ(diff[0], "Modified: " + filename);
}

TEST_F(FileSystemTestFix, SnapshotExportCarCreatesFile) {
  const std::string filename = "car_file.txt";
  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, "carcontent"));
  ASSERT_TRUE(fs.snapshotCreate("carSnap"));

  const std::string carPath = "/tmp/test_snapshot.car";
  ASSERT_TRUE(fs.snapshotExportCar("carSnap", carPath));

  struct stat st{};
  EXPECT_EQ(stat(carPath.c_str(), &st), 0);
  EXPECT_GT(st.st_size, 0);
  std::remove(carPath.c_str());
}

// Test for the original extendedAttributes test logic, ensuring general xattr
// functions work
TEST_F(FileSystemTestFix, ExtendedAttributeGeneralOperations) {
  const std::string filename = "xattr_general_ops.txt";
  const std::string attr_name = "user.test_attr";
  const std::string attr_value1 = "value1";
  const std::string attr_value2 = "value2";

  ASSERT_TRUE(fs.createFile(filename)); // Create file first
  // Note: writeFile is not called, so no pipeline xattrs (cid, nonce etc) are
  // set initially.

  // Set and Get
  fs.setXattr(filename, attr_name, attr_value1);
  EXPECT_EQ(fs.getXattr(filename, attr_name), attr_value1);

  // Get non-existent attribute
  EXPECT_EQ(fs.getXattr(filename, "user.non_existent_attr_test"), "");

  // Overwrite
  fs.setXattr(filename, attr_name, attr_value2);
  EXPECT_EQ(fs.getXattr(filename, attr_name), attr_value2);

  // Set/Get on non-existent file (should be no-op for set, empty for get)
  fs.setXattr("NoFileHere.txt", "user.test", "value");
  EXPECT_EQ(fs.getXattr("NoFileHere.txt", "user.test"), "");
}

TEST_F(FileSystemTestFix, ReadOldFormatFile) {
  const std::string filename = "OldFormat.txt";
  const std::string old_content = "This is an old format file content.";

  // Simulate an old file by manually inserting it into _Files and not setting
  // pipeline xattrs This requires friend class or making _Files public for
  // tests, which is not ideal. The current FileSystem::readFile has a
  // heuristic: if (!compressedData.empty() && cid.empty()) { return
  // bytes_to_string(compressedData); } So, if we createFile and then manually
  // set _Files[_pFilename] = string_to_bytes(old_content) without setting
  // xattrs, it should be treated as an old file. This test cannot be perfectly
  // implemented without modifying FileSystem for testability or using the
  // existing writeFile which now always applies the new pipeline.

  // For now, we test the behavior of reading a file that has data but no
  // 'user.cid' xattr. This can be achieved by creating a file, then manually
  // clearing the CID xattr (if possible) or relying on the heuristic for files
  // that were never processed by the new writeFile.

  ASSERT_TRUE(fs.createFile(filename));
  // Manually set content to bypass new writeFile pipeline. This is tricky.
  // The best we can do with current public API is to write, then remove CID.
  ASSERT_TRUE(
      fs.writeFile(filename, old_content)); // This writes with NEW pipeline
  std::string current_cid = fs.getXattr(filename, "user.cid");
  ASSERT_FALSE(current_cid.empty());

  // Simulate missing CID for an "old" file by removing it.
  // Need a method to remove a single xattr for a clean test, or modify setXattr
  // to take empty value as delete. For now, setting it to empty to simulate
  // absence for the readFile logic.
  fs.setXattr(filename, "user.cid", "");
  // Also clear other pipeline xattrs to make it look more like an old file
  fs.setXattr(filename, "user.nonce", "");
  fs.setXattr(filename, "user.original_size", "");
  fs.setXattr(filename, "user.encrypted_size", "");

  // Now, readFile should hit the "old format" path if the data itself is stored
  // raw (which it isn't with current writeFile) The current writeFile stores
  // processed data. So, this test will actually try to decrypt/decompress the
  // already processed data using missing metadata, which should fail and return
  // empty. The "old format" handling in readFile is for data that was stored
  // *before* the pipeline. This test, as is, will likely result in readFile
  // returning "" due to other errors. A true test of old format reading would
  // require populating _Files directly.

  // Given the current FileSystem::readFile logic:
  // 1. It will find missing xattrs.
  // 2. It will throw "Missing required metadata..."
  // 3. The catch block will return "".
  EXPECT_EQ(fs.readFile(filename), "");
  // This isn't testing reading old raw data, but rather how it handles missing
  // metadata for new data.
}

TEST_F(FileSystemTestFix, SaveLoadState) {
  const std::string filename = "Persist.txt";
  ASSERT_TRUE(fs.createFile(filename));
  ASSERT_TRUE(fs.writeFile(filename, "hello"));
  ASSERT_TRUE(fs.saveState("fs_state.yaml"));

  FileSystem other;
  simplidfs::KeyManager::getInstance().initialize();
  ASSERT_TRUE(other.loadState("fs_state.yaml"));
  EXPECT_EQ(other.readFile(filename), "hello");
  std::remove("fs_state.yaml");
}
