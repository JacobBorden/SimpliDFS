#include <gtest/gtest.h>
#include "message.h"

TEST(MessageTests, SerializeMessage)
{
	Message msg;
	msg._Type = MessageType::CreateFile;
	msg._Filename = "Test";
	msg._Content = "Test";
	std::string result = SerializeMessage(msg);
	std::string expected = "0|Test|Test";
	ASSERT_EQ(result, expected);
}

TEST(MessageTests, DeserializeMessage)
{
	std::string serialized = "0|File|Content";
	Message msg = DeserializeMessage(serialized);

	ASSERT_EQ(msg._Type, MessageType::CreateFile);
	ASSERT_EQ(msg._Filename, "File");
	ASSERT_EQ(msg._Content, "Content");
}
