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

TEST(FileSystemTests, writeFile)
{
	FileSystem fs;
	fs.createFile("Test");
	bool first = fs.writeFile("Test", "Test");
	bool second = fs.writeFile("Test2", "Test");

	ASSERT_EQ(first,true);
	ASSERT_EQ(second,false);

}


TEST(FileSystemTests, readFile)
{
	FileSystem fs;
	fs.createFile("Test");
	fs.writeFile("Test", "Read Test");
	std::string data = fs.readFile("Test");

	ASSERT_EQ(data, "Read Test");
	 	

}
