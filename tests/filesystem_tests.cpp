#include <gtest/gtest.h>
#include "filesystem.h"

TEST(FileSystemTests, createFile)
{
	FileSystem fs;
	bool first = fs.createFile("Test");
	bool second = fs.createFile("Test");
	
	ASSERT_EQ(first, true);
	ASSERT_EQ(second, false);
}
