#include "message.h"
#include <sstream>

std::string SerializeMessage(const Message& _pMessage)
{
	std::stringstream ss;
	ss << static_cast<int>(_pMessage._Type) << "|"
	   << _pMessage._Filename << "|"
	   << _pMessage._Content;

	std::string messageString = ss.str();
	return messageString;
}

Message DeserializeMessage(const std::string& _pSerializedMessage)
{
	std::istringstream iss(_pSerializedMessage);
	std::string token;
	Message msg;
	
	std::getline(iss, token, '|');
	msg._Type = static_cast<MessageType>(std::stoi(token));
	
	std::getline(iss, token, '|');
	msg._Filename = token;

	std::getline(iss, token, '|');
	msg._Content = token;

	return msg;

}
