#include <gtest/gtest.h>
#include "message.h"

TEST(MessageTests, SerializeMessage)
{
	Message msg;
	msg._Type = MessageType::CreateFile;
	msg._Filename = "Test";
	msg._Content = "TestContent";
    msg._NodeAddress = "127.0.0.1";
    msg._NodePort = 8080;
	std::string result = Message::Serialize(msg);
	// Expected format: Type|Filename|Content|NodeAddress|NodePort
	std::string expected = "0|Test|TestContent|127.0.0.1|8080";
	ASSERT_EQ(result, expected);
}

TEST(MessageTests, DeserializeMessage)
{
    // Expected format: Type|Filename|Content|NodeAddress|NodePort
	std::string serialized = "0|File|Content|192.168.0.1|9090";
	Message msg = Message::Deserialize(serialized);

	ASSERT_EQ(msg._Type, MessageType::CreateFile);
	ASSERT_EQ(msg._Filename, "File");
	ASSERT_EQ(msg._Content, "Content");
    ASSERT_EQ(msg._NodeAddress, "192.168.0.1");
    ASSERT_EQ(msg._NodePort, 9090);
}
