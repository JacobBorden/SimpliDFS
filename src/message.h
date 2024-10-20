#pragma once
#ifndef _SIMPLIDFS_MESSAGE_H
#define _SIMPLIDFS_MESSAGE_H

#include <string>

enum class MessageType{
	CreateFile,
	WriteFile,
	ReadFile,
	RemoveFile,
	FileCreated,
	FileWritten,
	FileRead,
	FileRemoved
};

struct Message{
	MessageType _Type;
	std::string _Filename;
	std::string _Content;
};

std::string SerializeMessage(const Message& _pMessage);
Message DeserializeMessage(const std::string& _pSerializedMessage);

#endif
