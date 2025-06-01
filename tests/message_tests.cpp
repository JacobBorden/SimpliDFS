#include <gtest/gtest.h>
#include "utilities/message.h"
#include <string> // Required for std::to_string

// Helper function to create a default expected string for a given MessageType
// Assumes all fields are default (empty strings, zero for numerics) unless specified.
// Order: Type|Filename|Content|NodeAddress|NodePort|ErrorCode|Mode|Uid|Gid|Offset|Size|Data|Path|NewPath
std::string CreateExpectedString(MessageType type,
                                 const std::string& filename = "",
                                 const std::string& content = "",
                                 const std::string& node_address = "",
                                 int node_port = 0,
                                 int error_code = 0,
                                 uint32_t mode = 0,
                                 uint32_t uid = 0,
                                 uint32_t gid = 0,
                                 int64_t offset = 0,
                                 uint64_t size = 0,
                                 const std::string& data = "",
                                 const std::string& path = "",
                                 const std::string& new_path = "") {
    std::string expected = std::to_string(static_cast<int>(type)) + "|";
    expected += filename + "|";
    expected += content + "|";
    expected += node_address + "|";
    expected += std::to_string(node_port) + "|";
    expected += std::to_string(error_code) + "|";
    expected += std::to_string(mode) + "|";
    expected += std::to_string(uid) + "|";
    expected += std::to_string(gid) + "|";
    expected += std::to_string(offset) + "|";
    expected += std::to_string(size) + "|";
    expected += data + "|";
    expected += path + "|";
    expected += new_path;
    return expected;
}


TEST(MessageTests, SerializeDeserializeDefault) {
    Message msg;
    // Fill with non-default values to ensure they are reset or handled if not set by specific tests
    msg._Type = MessageType::CreateFile; // Will be overwritten by specific tests
    msg._Filename = "DefaultFile";
    msg._Content = "DefaultContent";
    msg._NodeAddress = "1.1.1.1";
    msg._NodePort = 111;
    msg._ErrorCode = 1;
    msg._Mode = 1u;
    msg._Uid = 1u;
    msg._Gid = 1u;
    msg._Offset = 1;
    msg._Size = 1ULL;
    msg._Data = "DefaultData";
    msg._Path = "/default";
    msg._NewPath = "/newdefault";

    // Test with a generic type not specifically tested below, using mostly defaults
    msg = {}; // Reset to defaults
    msg._Type = MessageType::Heartbeat;
    msg._Filename = "node123"; // Heartbeat uses _Filename for NodeID

    std::string expected_str = CreateExpectedString(MessageType::Heartbeat, "node123");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Heartbeat);
    ASSERT_EQ(deserialized_msg._Filename, "node123");
    ASSERT_EQ(deserialized_msg._Content, "");
    ASSERT_EQ(deserialized_msg._NodeAddress, "");
    ASSERT_EQ(deserialized_msg._NodePort, 0);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Mode, 0u);
    ASSERT_EQ(deserialized_msg._Uid, 0u);
    ASSERT_EQ(deserialized_msg._Gid, 0u);
    ASSERT_EQ(deserialized_msg._Offset, 0);
    ASSERT_EQ(deserialized_msg._Size, 0ULL);
    ASSERT_EQ(deserialized_msg._Data, "");
    ASSERT_EQ(deserialized_msg._Path, "");
    ASSERT_EQ(deserialized_msg._NewPath, "");
}


TEST(MessageTests, GetAttr) {
    Message msg;
    msg._Type = MessageType::GetAttr;
    msg._Path = "/test/file.txt";

    std::string expected_str = CreateExpectedString(MessageType::GetAttr, "", "", "", 0, 0, 0, 0, 0, 0, 0, "", "/test/file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::GetAttr);
    ASSERT_EQ(deserialized_msg._Path, "/test/file.txt");
}

TEST(MessageTests, GetAttrResponse) {
    Message msg;
    msg._Type = MessageType::GetAttrResponse;
    msg._ErrorCode = 0;
    msg._Mode = 33188; // S_IFREG | 0644
    msg._Uid = 1000;
    msg._Gid = 1000;
    msg._Size = 1024;

    std::string expected_str = CreateExpectedString(MessageType::GetAttrResponse, "", "", "", 0, 0, 33188, 1000, 1000, 0, 1024);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::GetAttrResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Mode, 33188u);
    ASSERT_EQ(deserialized_msg._Uid, 1000u);
    ASSERT_EQ(deserialized_msg._Gid, 1000u);
    ASSERT_EQ(deserialized_msg._Size, 1024ULL);
}

TEST(MessageTests, Readdir) {
    Message msg;
    msg._Type = MessageType::Readdir;
    msg._Path = "/testdir";

    std::string expected_str = CreateExpectedString(MessageType::Readdir, "", "", "", 0, 0, 0, 0, 0, 0, 0, "", "/testdir");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Readdir);
    ASSERT_EQ(deserialized_msg._Path, "/testdir");
}

TEST(MessageTests, ReaddirResponse) {
    Message msg;
    msg._Type = MessageType::ReaddirResponse;
    msg._ErrorCode = 0;
    msg._Data = "file1.txt\0dir1\0file2.log\0"; // Null-separated entries

    std::string expected_str = CreateExpectedString(MessageType::ReaddirResponse, "", "", "", 0, 0, 0, 0, 0, 0, 0, "file1.txt\0dir1\0file2.log\0");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::ReaddirResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Data, "file1.txt\0dir1\0file2.log\0");
}

TEST(MessageTests, Access) {
    Message msg;
    msg._Type = MessageType::Access;
    msg._Path = "/test/file.txt";
    msg._Mode = R_OK; // Example access mode (typically int, fits in uint32_t)

    std::string expected_str = CreateExpectedString(MessageType::Access, "", "", "", 0, 0, R_OK, 0, 0, 0, 0, "", "/test/file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Access);
    ASSERT_EQ(deserialized_msg._Path, "/test/file.txt");
    ASSERT_EQ(deserialized_msg._Mode, (uint32_t)R_OK);
}

TEST(MessageTests, AccessResponse) {
    Message msg;
    msg._Type = MessageType::AccessResponse;
    msg._ErrorCode = EACCES;

    std::string expected_str = CreateExpectedString(MessageType::AccessResponse, "", "", "", 0, EACCES);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::AccessResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, EACCES);
}


TEST(MessageTests, Open) {
    Message msg;
    msg._Type = MessageType::Open;
    msg._Path = "/test/file.txt";
    msg._Mode = O_RDWR;

    std::string expected_str = CreateExpectedString(MessageType::Open, "", "", "", 0, 0, O_RDWR, 0, 0, 0, 0, "", "/test/file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Open);
    ASSERT_EQ(deserialized_msg._Path, "/test/file.txt");
    ASSERT_EQ(deserialized_msg._Mode, (uint32_t)O_RDWR);
}

TEST(MessageTests, OpenResponse) {
    Message msg;
    msg._Type = MessageType::OpenResponse;
    msg._ErrorCode = 0;
    // Optionally, _Size could represent a file handle if Message struct evolves
    // msg._Size = 12345; // Example file handle

    std::string expected_str = CreateExpectedString(MessageType::OpenResponse); // Assuming FH is not used via _Size for now
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::OpenResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

TEST(MessageTests, CreateFileResponse) { // From Metaserver to Client
    Message msg;
    msg._Type = MessageType::CreateFileResponse;
    msg._ErrorCode = 0;
    // msg._Path = "/newly/created.txt"; // Path could be part of response

    std::string expected_str = CreateExpectedString(MessageType::CreateFileResponse);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::CreateFileResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}


TEST(MessageTests, Read) {
    Message msg;
    msg._Type = MessageType::Read;
    msg._Path = "/test/file.txt";
    msg._Offset = 1024;
    msg._Size = 4096; // Bytes to read

    std::string expected_str = CreateExpectedString(MessageType::Read, "", "", "", 0, 0, 0, 0, 0, 1024, 4096, "", "/test/file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Read);
    ASSERT_EQ(deserialized_msg._Path, "/test/file.txt");
    ASSERT_EQ(deserialized_msg._Offset, 1024);
    ASSERT_EQ(deserialized_msg._Size, 4096ULL);
}

TEST(MessageTests, ReadResponse) {
    Message msg;
    msg._Type = MessageType::ReadResponse;
    msg._ErrorCode = 0;
    msg._Data = "This is file content read from node.";
    msg._Size = msg._Data.length(); // Actual bytes read

    std::string expected_str = CreateExpectedString(MessageType::ReadResponse, "", "", "", 0, 0, 0, 0, 0, 0, msg._Data.length(), msg._Data);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::ReadResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Data, "This is file content read from node.");
    ASSERT_EQ(deserialized_msg._Size, msg._Data.length());
}


TEST(MessageTests, Write) {
    Message msg;
    msg._Type = MessageType::Write;
    msg._Path = "/test/file.txt";
    msg._Offset = 512;
    msg._Data = "Data to be written.";
    msg._Size = msg._Data.length();

    std::string expected_str = CreateExpectedString(MessageType::Write, "", "", "", 0, 0, 0, 0, 0, 512, msg._Data.length(), msg._Data, "/test/file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Write);
    ASSERT_EQ(deserialized_msg._Path, "/test/file.txt");
    ASSERT_EQ(deserialized_msg._Offset, 512);
    ASSERT_EQ(deserialized_msg._Data, "Data to be written.");
    ASSERT_EQ(deserialized_msg._Size, msg._Data.length());
}

TEST(MessageTests, WriteResponse) {
    Message msg;
    msg._Type = MessageType::WriteResponse;
    msg._ErrorCode = 0;
    msg._Size = 19; // Bytes written confirmed by server/node

    std::string expected_str = CreateExpectedString(MessageType::WriteResponse, "", "", "", 0, 0, 0, 0, 0, 0, 19);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::WriteResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Size, 19ULL);
}

TEST(MessageTests, Unlink) {
    Message msg;
    msg._Type = MessageType::Unlink;
    msg._Path = "/test/to_delete.txt";

    std::string expected_str = CreateExpectedString(MessageType::Unlink, "", "", "", 0, 0, 0, 0, 0, 0, 0, "", "/test/to_delete.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Unlink);
    ASSERT_EQ(deserialized_msg._Path, "/test/to_delete.txt");
}

TEST(MessageTests, UnlinkResponse) {
    Message msg;
    msg._Type = MessageType::UnlinkResponse;
    msg._ErrorCode = 0;

    std::string expected_str = CreateExpectedString(MessageType::UnlinkResponse);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::UnlinkResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

TEST(MessageTests, Rename) {
    Message msg;
    msg._Type = MessageType::Rename;
    msg._Path = "/old/path.txt";
    msg._NewPath = "/new/path.txt";

    std::string expected_str = CreateExpectedString(MessageType::Rename, "", "", "", 0, 0, 0, 0, 0, 0, 0, "", "/old/path.txt", "/new/path.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Rename);
    ASSERT_EQ(deserialized_msg._Path, "/old/path.txt");
    ASSERT_EQ(deserialized_msg._NewPath, "/new/path.txt");
}

TEST(MessageTests, RenameResponse) {
    Message msg;
    msg._Type = MessageType::RenameResponse;
    msg._ErrorCode = 0;

    std::string expected_str = CreateExpectedString(MessageType::RenameResponse);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::RenameResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

TEST(MessageTests, Statx) {
    Message msg;
    msg._Type = MessageType::Statx;
    msg._Path = "/test/file_for_statx.txt";

    std::string expected_str = CreateExpectedString(MessageType::Statx, "", "", "", 0, 0, 0, 0, 0, 0, 0, "", "/test/file_for_statx.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Statx);
    ASSERT_EQ(deserialized_msg._Path, "/test/file_for_statx.txt");
}

TEST(MessageTests, StatxResponse) {
    Message msg;
    msg._Type = MessageType::StatxResponse;
    msg._ErrorCode = 0;
    msg._Mode = 33188;
    msg._Uid = 1001;
    msg._Gid = 1002;
    msg._Size = 2048;
    // msg._Data = "serialized_timestamps_if_any"; // For now, not testing data field for StatxResponse

    std::string expected_str = CreateExpectedString(MessageType::StatxResponse, "", "", "", 0, 0, 33188, 1001, 1002, 0, 2048);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::StatxResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Mode, 33188u);
    ASSERT_EQ(deserialized_msg._Uid, 1001u);
    ASSERT_EQ(deserialized_msg._Gid, 1002u);
    ASSERT_EQ(deserialized_msg._Size, 2048ULL);
    // ASSERT_EQ(deserialized_msg._Data, "serialized_timestamps_if_any");
}

TEST(MessageTests, Utimens) {
    Message msg;
    msg._Type = MessageType::Utimens;
    msg._Path = "/test/timestamp_file.txt";
    msg._Data = "1678886400:0|1678886460:0"; // atime|mtime

    std::string expected_str = CreateExpectedString(MessageType::Utimens, "", "", "", 0, 0, 0, 0, 0, 0, 0, "1678886400:0|1678886460:0", "/test/timestamp_file.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::Utimens);
    ASSERT_EQ(deserialized_msg._Path, "/test/timestamp_file.txt");
    ASSERT_EQ(deserialized_msg._Data, "1678886400:0|1678886460:0");
}

TEST(MessageTests, UtimensResponse) {
    Message msg;
    msg._Type = MessageType::UtimensResponse;
    msg._ErrorCode = 0;

    std::string expected_str = CreateExpectedString(MessageType::UtimensResponse);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::UtimensResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

TEST(MessageTests, NodeReadFileChunk) {
    Message msg;
    msg._Type = MessageType::NodeReadFileChunk;
    msg._Filename = "chunk_file.dat";
    msg._Offset = 4096;
    msg._Size = 1024; // Bytes to read

    std::string expected_str = CreateExpectedString(MessageType::NodeReadFileChunk, "chunk_file.dat", "", "", 0, 0, 0, 0, 0, 4096, 1024);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::NodeReadFileChunk);
    ASSERT_EQ(deserialized_msg._Filename, "chunk_file.dat");
    ASSERT_EQ(deserialized_msg._Offset, 4096);
    ASSERT_EQ(deserialized_msg._Size, 1024ULL);
}

TEST(MessageTests, NodeReadFileChunkResponse) {
    Message msg;
    msg._Type = MessageType::NodeReadFileChunkResponse;
    msg._ErrorCode = 0;
    msg._Data = "chunk_data_content";
    msg._Size = msg._Data.length(); // Actual bytes read

    std::string expected_str = CreateExpectedString(MessageType::NodeReadFileChunkResponse, "", "", "", 0, 0, 0, 0, 0, 0, msg._Data.length(), msg._Data);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::NodeReadFileChunkResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Data, "chunk_data_content");
    ASSERT_EQ(deserialized_msg._Size, msg._Data.length());
}

TEST(MessageTests, NodeWriteFileChunk) {
    Message msg;
    msg._Type = MessageType::NodeWriteFileChunk;
    msg._Filename = "chunk_file_to_write.dat";
    msg._Offset = 0;
    msg._Data = "data_for_chunk_write";
    msg._Size = msg._Data.length();

    std::string expected_str = CreateExpectedString(MessageType::NodeWriteFileChunk, "chunk_file_to_write.dat", "", "", 0, 0, 0, 0, 0, 0, msg._Data.length(), msg._Data);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::NodeWriteFileChunk);
    ASSERT_EQ(deserialized_msg._Filename, "chunk_file_to_write.dat");
    ASSERT_EQ(deserialized_msg._Offset, 0);
    ASSERT_EQ(deserialized_msg._Data, "data_for_chunk_write");
    ASSERT_EQ(deserialized_msg._Size, msg._Data.length());
}

TEST(MessageTests, NodeWriteFileChunkResponse) {
    Message msg;
    msg._Type = MessageType::NodeWriteFileChunkResponse;
    msg._ErrorCode = 0;
    msg._Size = 20; // Bytes written

    std::string expected_str = CreateExpectedString(MessageType::NodeWriteFileChunkResponse, "", "", "", 0, 0, 0, 0, 0, 0, 20);
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::NodeWriteFileChunkResponse);
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
    ASSERT_EQ(deserialized_msg._Size, 20ULL);
}

TEST(MessageTests, ReplicateFileCommand) {
    Message msg;
    msg._Type = MessageType::ReplicateFileCommand;
    msg._Filename = "file_to_replicate.dat";
    msg._NodeAddress = "192.168.1.101:5000"; // Target node address
    msg._Content = "targetNodeID1";         // Target node ID

    std::string expected_str = CreateExpectedString(MessageType::ReplicateFileCommand, "file_to_replicate.dat", "targetNodeID1", "192.168.1.101:5000");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::ReplicateFileCommand);
    ASSERT_EQ(deserialized_msg._Filename, "file_to_replicate.dat");
    ASSERT_EQ(deserialized_msg._NodeAddress, "192.168.1.101:5000");
    ASSERT_EQ(deserialized_msg._Content, "targetNodeID1");
}

TEST(MessageTests, ReceiveFileCommand) {
    Message msg;
    msg._Type = MessageType::ReceiveFileCommand;
    msg._Filename = "file_to_receive.dat";
    msg._NodeAddress = "192.168.1.100:5000"; // Source node address
    msg._Content = "sourceNodeID1";          // Source node ID

    std::string expected_str = CreateExpectedString(MessageType::ReceiveFileCommand, "file_to_receive.dat", "sourceNodeID1", "192.168.1.100:5000");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::ReceiveFileCommand);
    ASSERT_EQ(deserialized_msg._Filename, "file_to_receive.dat");
    ASSERT_EQ(deserialized_msg._NodeAddress, "192.168.1.100:5000");
    ASSERT_EQ(deserialized_msg._Content, "sourceNodeID1");
}

TEST(MessageTests, FileCreated) { // Node to Metaserver
    Message msg;
    msg._Type = MessageType::FileCreated;
    msg._Filename = "new_file_on_node.txt";
    msg._ErrorCode = 0;

    std::string expected_str = CreateExpectedString(MessageType::FileCreated, "new_file_on_node.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::FileCreated);
    ASSERT_EQ(deserialized_msg._Filename, "new_file_on_node.txt");
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

TEST(MessageTests, FileRemoved) { // Node to Metaserver
    Message msg;
    msg._Type = MessageType::FileRemoved;
    msg._Filename = "deleted_file_on_node.txt";
    msg._ErrorCode = 0;

    std::string expected_str = CreateExpectedString(MessageType::FileRemoved, "deleted_file_on_node.txt");
    std::string serialized_str = Message::Serialize(msg);
    ASSERT_EQ(serialized_str, expected_str);

    Message deserialized_msg = Message::Deserialize(serialized_str);
    ASSERT_EQ(deserialized_msg._Type, MessageType::FileRemoved);
    ASSERT_EQ(deserialized_msg._Filename, "deleted_file_on_node.txt");
    ASSERT_EQ(deserialized_msg._ErrorCode, 0);
}

// Test for empty fields and edge cases in deserialization
TEST(MessageTests, DeserializeWithEmptyFields) {
    std::string serialized = "0||||0|0|0|0|0|0|0|||"; // CreateFile with all string fields empty
    Message msg = Message::Deserialize(serialized);
    ASSERT_EQ(msg._Type, MessageType::CreateFile);
    ASSERT_EQ(msg._Filename, "");
    ASSERT_EQ(msg._Content, "");
    ASSERT_EQ(msg._NodeAddress, "");
    ASSERT_EQ(msg._NodePort, 0);
    ASSERT_EQ(msg._ErrorCode, 0);
}

TEST(MessageTests, DeserializeWithOnlyType) {
    // This tests a potentially malformed message, ensure it doesn't crash and sets defaults
    // The current deserializer might throw if delimiters are missing.
    // This test assumes the deserializer is robust enough or expects specific behavior for short strings.
    // Based on current Deserialize, this will likely throw.
    std::string serialized = std::to_string(static_cast<int>(MessageType::Heartbeat));
    ASSERT_THROW(Message::Deserialize(serialized), std::runtime_error);
}

TEST(MessageTests, DeserializeWithTooFewFields) {
    std::string serialized = "1|file.txt|content"; // Missing many fields
    ASSERT_THROW(Message::Deserialize(serialized), std::runtime_error);
}
