#pragma once

#ifndef _NET_CLIENT_
#define _NET_CLIENT_

#ifdef _WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment("Ws2_32.lib")

#ifndef _NETWORK_MACROS_
#define _NETWORK_MACROS
#define VERSIONREQUESTED MAKEWORD(2,2)
#define INVALIDSOCKET(s) ((s) == INVALID_SOCKET)
#define GETERROR() (WSAGetLastError())
#define CLOSESOCKET(s) (closesocket(s))
#endif

#else
#include  <sys/socket.h>
#include <netinet/in.h>
#include  <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <bits/stdc++.h>
#include <netdb.h>

#ifndef _NETWORK_MACROS_
#define _NETWORK_MACROS_
#define SOCKET int
#define SOCKET_ERROR -1
#define INVALIDSOCKET(s) ((s) < 0)
#define GETERROR() (errno)
#define CLOSESOCKET(s) (close(s))
#define ZeroMemory(dest, count) (memset(dest,0,count))
typedef const char* PCSTR;
#endif
#endif

#include <iostream>
#include <string>
#include <vector>

namespace Networking {

class Client {
public:
// Constructs a new client object with no host or port specified.
Client();

// Constructs a new client object with the specified host and port.
// The client will automatically connect to the host on the specified port.
Client(PCSTR _pHost, int _pPortNumber);

// Destroys the client object and cleans up any resources it was using.
~Client();

// Initializes the client socket.
bool InitClientSocket();

// Creates a TCP client socket with the specified host and port.
bool CreateClientTCPSocket(PCSTR _pHost, int pPort);

// Creates a UDP client socket with the specified host and port.
bool CreateClientUDPSocket(PCSTR _pHost, int _pPort);

// Creates a client socket with the specified host, port, socket type,
// and protocol.
bool CreateClientSocket(PCSTR _pHost, int _pPort);

// Connects the client socket to the specified host on the specified port.
bool ConnectClientSocket();

// Sets the socket type for the client socket.
void SetSocketType(int _pSocktype);

// Sets the protocol for the client socket.
void SetProtocol(int _pProtocol);

// Sets the address family for the client socket.
void SetFamily(int _pFamily);

// Sends the specified data buffer to the connected host.
int Send(PCSTR _pSendBuffer);

// Send data to a specified address and port
int SendTo(PCSTR pBuffer, PCSTR pAddress, int pPort);

// Send a file to the server
void SendFile(const std::string& _pFilePath);

// Receives data from the connected host and stores it in a vector.
std::vector<char> Receive();

// Receive data from a specified address and port
std::vector<char> ReceiveFrom(PCSTR _pAddress, int _pPort);

// Receive a file from the server
void   ReceiveFile(const std::string& _pFilePath);

// Disconnects the client socket from the host.
bool Disconnect();

// Returns whether the client is currently connected to a host.
bool IsConnected();

// Get the hostname of the client
std::string GetHostName();

// Get the hostname of the server
std::string GetServerHostName();

// Get the local IP address of the client
std::string GetLocalIPAddress();

// Get the remote IP address of the server
std::string GetRemoteIPAddress();

private:
// Whether the client is currently connected to a host.
bool clientIsConnected = false;

// The client socket.
SOCKET connectionSocket;

// Windows-specific data for socket initialization.
    #ifdef _WIN32
WSADATA wsaData;
    #endif

// Address information for the client socket.
addrinfo addressInfo;
addrinfo* hostAddressInfo;

};

}

#endif