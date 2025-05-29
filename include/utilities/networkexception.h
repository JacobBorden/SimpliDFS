#pragma once
#ifndef _NETWORK_EXCEPTION_
#define _NETWORK_EXCEPTION
#ifndef _WIN32
#ifndef SOCKET
#define SOCKET int
#endif
#else
#include <WinSock2.h>
#endif

#include <exception>
#include <string>

namespace Networking
{
class NetworkException : public std::exception
{
public:
NetworkException(SOCKET socket, int errorCode, const std::string& message) :
	m_socket(socket), m_errorCode(errorCode), m_message(message) {
}

const char*  what() const noexcept override {
	return m_message.c_str();
}

SOCKET GetSocket(){
	return m_socket;
}

int GetErrorCode(){
	return m_errorCode;
}

private:
SOCKET m_socket;
int m_errorCode;
std::string m_message;
};

}
#endif