#include "utilities/message.h"
#include <gtest/gtest.h>

TEST(MessageTests, SerializeMessage) {
  Message msg;
  msg._Type = MessageType::CreateFile;
  msg._Filename = "Test";
  msg._Content = "TestContent";
  msg._NodeAddress = "127.0.0.1";
  msg._NodePort = 8080;
  std::string result = Message::Serialize(msg);
  // Expected format:
  // Type|Filename|Content|NodeAddress|NodePort|ErrorCode|Mode|Uid|Gid|Offset|Size|Data|Path|NewPath
  std::string expected = "0|Test|TestContent|127.0.0.1|8080|0|0|0|0|0|0|||";
  ASSERT_EQ(result, expected);
}

TEST(MessageTests, DeserializeMessage) {
  // Format:
  // Type|Filename|Content|NodeAddress|NodePort|ErrorCode|Mode|Uid|Gid|Offset|Size|Data|Path|NewPath
  std::string serialized = "0|File|Content|192.168.0.1|9090|0|0|0|0|0|0|||";
  Message msg = Message::Deserialize(serialized);

  ASSERT_EQ(msg._Type, MessageType::CreateFile);
  ASSERT_EQ(msg._Filename, "File");
  ASSERT_EQ(msg._Content, "Content");
  ASSERT_EQ(msg._NodeAddress, "192.168.0.1");
  ASSERT_EQ(msg._NodePort, 9090);
  ASSERT_EQ(msg._ErrorCode, 0);
  ASSERT_EQ(msg._Mode, 0u); // u suffix for unsigned
  ASSERT_EQ(msg._Uid, 0u);
  ASSERT_EQ(msg._Gid, 0u);
  ASSERT_EQ(msg._Offset, 0);
  ASSERT_EQ(msg._Size, 0ULL); // ULL for unsigned long long
  ASSERT_EQ(msg._Data, "");
  ASSERT_EQ(msg._Path, "");
  ASSERT_EQ(msg._NewPath, "");
}

TEST(MessageTests, DeserializePartialMessage) {
  std::string serialized = "1|test.txt|data"; // Old-style short format
  EXPECT_THROW({ Message::Deserialize(serialized); }, std::runtime_error);
}
