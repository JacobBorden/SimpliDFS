#include <gtest/gtest.h>
#include "utilities/message.h"

TEST(MessageTests, SerializeMessage)
{
	Message msg;
	msg._Type = MessageType::CreateFile;
	msg._Filename = "Test";
	msg._Content = "TestContent";
    msg._NodeAddress = "127.0.0.1";
    msg._NodePort = 8080;
	std::string result = Message::Serialize(msg);
	// Expected format: Type|Filename|Content|NodeAddress|NodePort|ErrorCode|Mode|Uid|Gid|Offset|Size|Data|Path|NewPath
	std::string expected = "0|Test|TestContent|127.0.0.1|8080|0|0|0|0|0|0|||";
	ASSERT_EQ(result, expected);
}

TEST(MessageTests, DeserializeMessage)
{
    // Format: Type|Filename|Content|NodeAddress|NodePort|ErrorCode|Mode|Uid|Gid|Offset|Size|Data|Path|NewPath
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

TEST(MessageTests, BinaryStringFieldsRoundTrip) {
  std::string bytes;
  for (unsigned int value = 0; value < 256; ++value)
    bytes.push_back(static_cast<char>(value));

  Message request{};
  request._Type = MessageType::WriteFile;
  request._Filename = "file|name";
  request._Content = bytes;
  request._NodeAddress = "host%00";
  request._Offset = 4095;
  request._Size = bytes.size();
  request._Data = std::string(1, '\0');
  request._Path = "/file|name";
  request._NewPath = "/new\nname";

  const auto wire = Message::Serialize(request);
  EXPECT_EQ(wire.size(), std::char_traits<char>::length(wire.c_str()));
  const auto decoded = Message::Deserialize(wire);
  EXPECT_EQ(decoded._Type, request._Type);
  EXPECT_EQ(decoded._Filename, request._Filename);
  EXPECT_EQ(decoded._Content, request._Content);
  EXPECT_EQ(decoded._NodeAddress, request._NodeAddress);
  EXPECT_EQ(decoded._Offset, request._Offset);
  EXPECT_EQ(decoded._Size, request._Size);
  EXPECT_EQ(decoded._Data, request._Data);
  EXPECT_EQ(decoded._Path, request._Path);
  EXPECT_EQ(decoded._NewPath, request._NewPath);
}

TEST(MessageTests, PreserveLegacyPercentAndMultilineData) {
  const std::string legacy = "1|||localhost|0|0|0|0|0|3|6|%00\nxy|/test|";
  const auto decoded = Message::Deserialize(legacy);
  EXPECT_EQ(decoded._Data, "%00\nxy");
  EXPECT_EQ(Message::Serialize(decoded), legacy);
}

TEST(MessageTests, RejectMalformedEscapedFields) {
  EXPECT_THROW(Message::Deserialize("escaped1|1||||0|0|0|0|0|0|%0|/test|"),
               std::runtime_error);
  EXPECT_THROW(Message::Deserialize("escaped1|1||||0|0|0|0|0|0|%GG|/test|"),
               std::runtime_error);
  EXPECT_THROW(Message::Deserialize("escaped1|1||||0|0|0|0|0|0||/test"),
               std::runtime_error);
  EXPECT_THROW(Message::Deserialize("escaped1|1||||0|0|0|0|0|0|x|y|/test|"),
               std::runtime_error);
  EXPECT_THROW(Message::Deserialize(""), std::runtime_error);
}
