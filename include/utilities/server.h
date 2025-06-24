#pragma once
#ifndef _NET_SERVER_
#define _NET_SERVER_
#ifdef _WIN32
#include <WinSock2.h>
#include <ws2tcpip.h>
#pragma comment("Ws2_32.lib")
#ifndef _NETWORK_MACROS_
#define _NETWORK_MACROS
#define VERSIONREQUESTED MAKEWORD(2, 2)
#define INVALIDSOCKET(s) ((s) == INVALID_SOCKET)
#define GETERROR() (WSAGetLastError())
#define CLOSESOCKET(s) (closesocket(s))
#endif
#else
#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef _NETWORK_MACROS_
#define _NETWORK_MACROS_
#define SOCKET int
#define SOCKET_ERROR -1
#define INVALIDSOCKET(s) ((s) < 0)
#define GETERROR() (errno)
#define CLOSESOCKET(s) (close(s))
#define ZeroMemory(dest, count) (memset(dest, 0, count))
typedef const char *PCSTR;
#endif
#endif
#ifndef MAX_RETRIES
#define MAX_RETRIES 3
#endif
#ifndef RETRY_DELAY
#define RETRY_DELAY 5
#endif
#include "utilities/errorcodes.h"
#include "utilities/logger.h"
#include "utilities/networkexception.h"
#include <chrono>
#include <iostream>
#include <mutex> // Added for std::mutex
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <thread>
#include <vector>

namespace Networking {

enum ServerType { IPv4, IPv6 };

// Struct to hold information about a connected client
struct ClientConnection {
  SOCKET clientSocket;
  sockaddr_in clientInfo;
  sockaddr_in6 clientInfo6;
  SSL *ssl = nullptr; ///< TLS session for this client
  bool operator==(const ClientConnection &other) const {
    // Compare the clientSocket member variables of the two objects
    return clientSocket == other.clientSocket;
  }
};

class Server {
public:
  // Constructor that takes in a port number and a server type.
  // This will only store parameters. Call startListening() to initialize and
  // bind.
  Server(int _pPortNumber = 8080, ServerType _pServerType = ServerType::IPv4);

  // Destructor
  ~Server();

  // Starts the server: Initializes, creates socket, binds, and listens.
  // Returns true on success, false on failure.
  bool startListening();

  // Initializes the server (e.g., WSAStartup on Windows)
  bool InitServer();

  // Creates a socket for the server using the stored port number and server
  // type
  bool
  CreateServerSocketInternal(); // Renamed to avoid confusion, takes no params

  void CreateSocket();   // Remains for internal socket creation step
  void BindSocket();     // Remains for internal bind step
  void ListenOnSocket(); // Remains for internal listen step

  // Listens for incoming client connections and returns a
  // Networking::ClientConnection object representing the connected client
  Networking::ClientConnection Accept();

  // Sets the socket type (used during CreateServerSocketInternal)
  void SetSocketType(int _pSockType);

  // Sets the socket family (used during CreateServerSocketInternal)
  void SetFamily(int _pFamily);

  // Sets the socket protocol (used during CreateServerSocketInternal)
  void SetProtocol(int _pProtocol);

  /** Enable TLS using the provided certificate and key files.
   *  Must be called before startListening().
   */
  bool enableTLS(const std::string &certFile = "",
                 const std::string &keyFile = "");

  // Sends data to a specific client
  int Send(PCSTR _pSendBuffer, Networking::ClientConnection _pClient);

  // Sends data to a specific address and port
  int SendTo(PCSTR _pBuffer, PCSTR _pAddress, int _pPort);

  // Sends data to all connected clients
  int SendToAll(PCSTR _pSendBuffer);

  // Sends a file to a specific client
  void SendFile(const std::string &_pFilePath,
                Networking::ClientConnection client);

  // Receives data from a specific client
  std::vector<char> Receive(Networking::ClientConnection client);

  // Receives data from a specific address and port
  std::vector<char> ReceiveFrom(PCSTR _pAddress, int _pPort);

  // Receives a file from a specific client
  void ReceiveFile(const std::string &_pFilePath,
                   Networking::ClientConnection client);

  // Returns true if the server is currently running and listening for
  // connections (i.e., startListening was successful and Shutdown hasn't been
  // called).
  bool ServerIsRunning();

  // Shut down the server
  void Shutdown();

  // Disconnects a specific client
  void DisconnectClient(Networking::ClientConnection _pClient);

  // Returns a vector of Networking::ClientConnection objects representing all
  // currently connected clients
  std::vector<Networking::ClientConnection> getClients() const;

  /**
   * @brief Check if a client is still connected.
   * @param client Client connection to check.
   * @return True if the client is in the active connection list.
   */
  bool isClientConnected(const Networking::ClientConnection &client) const;

  // Handles errors
  void ErrorHandling(NetworkException _pNetEx);

  std::string GetClientIPAddress(ClientConnection _pClient);
  ServerType GetServerType(); // Will use serverType_
  int GetPort();              // Will use portNumber_

private:
  // Member variables to store configuration
  int portNumber_;
  ServerType serverType_;

#ifdef _WIN32
  WSADATA wsaData; // Should this be a pointer or handled per InitServer call?
                   // For now, keeping as is, but WSAStartup/Cleanup should be
                   // balanced.
#endif
  addrinfo addressInfo; // Used for socket creation hints
  SOCKET serverSocket =
      0; // Listening socket, initialize to 0 or INVALIDSOCKET_CONST
  sockaddr_in serverInfo;   // Used for IPv4
  sockaddr_in6 serverInfo6; // Used when serverType_ == ServerType::IPv6
  bool serverIsConnected = false;
  std::vector<Networking::ClientConnection> clients;
  mutable std::mutex
      clients_mutex_; // Added mutable mutex for getClients() const

  bool useTLS = false;       ///< Whether TLS is enabled
  SSL_CTX *sslCtx = nullptr; ///< TLS context for accepting connections
  // Logger logger; // Removed - logging is done via Logger::getInstance() or
  // std::cout
};
} // namespace Networking

#endif
