#include "utilities/client.h" // include the header file for the client class
#include "utilities/networkexception.h" // Include the NetworkException header
#include "utilities/logger.h" // Include the Logger header
#include <string>   // Required for std::to_string
#include <thread>         // For std::this_thread::sleep_for
#include <chrono>         // For std::chrono::seconds, std::chrono::milliseconds
#include <cmath>          // For std::pow

namespace Networking {
    static const int kMaxRetries = 5;
    static const int kBaseBackoffDelayMs = 200; // Base delay for backoff in milliseconds
} // namespace Networking

// Constructor that initializes the client socket
Networking::Client::Client()
{
	try {
		// Initialize the client socket
		Networking::Client::InitClientSocket();
        Logger::getInstance().log(LogLevel::INFO, "Client initialized.");
	}

	// Catch any exceptions that are thrown
	catch(const Networking::NetworkException& e) {
		// Print the error code
        Logger::getInstance().log(LogLevel::ERROR, "Exception thrown during Client construction: " + std::string(e.what()));
		// std::cout<<"Exception thrown. Error Code"<<errorCode;
	}
}

// Constructor that initializes the client socket and connects to the server
Networking::Client::Client(PCSTR _pHost, int _pPortNumber)
{
	try{
		// Initialize the client socket
		Networking::Client::InitClientSocket();
		// Create a TCP socket
		Networking::Client::CreateClientTCPSocket(_pHost, _pPortNumber);
		// Connect to the server
		Networking::Client::ConnectClientSocket();
        Logger::getInstance().log(LogLevel::INFO, "Client initialized and connected to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber));
	}
	// Catch any exceptions that are thrown
	catch(const Networking::NetworkException& e) {
		// Print the error code
        Logger::getInstance().log(LogLevel::ERROR, "Exception thrown during Client construction with host/port: " + std::string(e.what()));
		// std::cout<<"Exception thrown. Error Code "<<errorCode;
	}
}

// Destructor for the client class
Networking::Client::~Client()
{
}

// Destructor for the client class
bool Networking::Client::InitClientSocket()
{
    #ifdef _WIN32
	// Initialize the Windows Sockets DLL
	int errorCode = WSAStartup(VERSIONREQUESTED, &this->wsaData);
	// If there was an error, throw an exception
	if(errorCode)
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client Initialization failed (WSAStartup)");
    #endif
	// If there was an error, throw an exception
	return true;
}

// Create a TCP socket
bool Networking::Client::CreateClientTCPSocket(PCSTR _pHost, int _pPort)
{
    // Clear the address info structure
    ZeroMemory(&addressInfo, sizeof(addressInfo));
    Networking::Client::SetFamily(AF_INET);
    Networking::Client::SetSocketType(SOCK_STREAM);
    Networking::Client::SetProtocol(IPPROTO_TCP);

    // Use hints to force IPv4
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(_pPort);
    int errorCode = getaddrinfo(_pHost, portStr.c_str(), &hints, &hostAddressInfo);

    if(errorCode)
    {
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client TCP socket creation failed (getaddrinfo)");
    }

    connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

    if(INVALIDSOCKET(connectionSocket))
    {
        int errorCode = GETERROR();
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client TCP socket creation failed (socket)");
    }
    return true;
}

bool Networking::Client::CreateClientUDPSocket(PCSTR _pHost, int _pPort)
{
	// Clear the address info structure
	ZeroMemory(&addressInfo, sizeof(addressInfo));
	// Set the socket family to AF_INET (IPv4)
	Networking::Client::SetFamily(AF_INET);
	Networking::Client::SetSocketType(SOCK_DGRAM); // Use SOCK_DGRAM for UDP sockets
	Networking::Client::SetProtocol(IPPROTO_UDP);

	// Get the address info for the specified host and port
	std::string portStr = std::to_string(_pPort);
	int errorCode = getaddrinfo(_pHost, portStr.c_str(),(const addrinfo*) &addressInfo,&hostAddressInfo );

// If there was an error, throw an exception
	if(errorCode)
	{
    #ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
    #endif
		// Throw the error code
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client UDP socket creation failed (getaddrinfo)");
	}

	// Create the UDP socket
	connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

	// If the socket is invalid, throw an exception
	if(INVALIDSOCKET(connectionSocket))
	{
		// Get the error code
		int errorCode = GETERROR();

	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client UDP socket creation failed (socket)");
	}
	// Return true if the UDP socket was created successfully
	return true;
}

bool Networking::Client::CreateClientSocket(PCSTR _pHost, int _pPort)
{
	std::string portStr = std::to_string(_pPort);
	int errorCode = getaddrinfo(_pHost, portStr.c_str(),(const addrinfo*) &addressInfo,&hostAddressInfo );

	if(errorCode)
	{
    #ifdef _WIN32
		WSACleanup();
    #endif
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client socket creation failed (getaddrinfo)");
	}

	connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

	if(INVALIDSOCKET(connectionSocket))
	{
		int errorCode = GETERROR();
	#ifdef _WIN32
		WSACleanup();
	#endif
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client socket creation failed (socket)");
	}
	return true;
}


// Connect to the server
bool Networking::Client::ConnectClientSocket()
{
    // Connect to the server using the hostAddressInfo from getaddrinfo
    int errorCode = connect(connectionSocket, hostAddressInfo->ai_addr, hostAddressInfo->ai_addrlen);
    if (errorCode)
    {
        errorCode = GETERROR();
        SOCKET tempSocket = connectionSocket; // Store for exception before closing
        CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(tempSocket, errorCode, "Client connect failed");
    }
    clientIsConnected = true;
    return true;
}

void Networking::Client::SetSocketType(int _pSocketType)
{
	addressInfo.ai_socktype = _pSocketType;
}

void Networking::Client::SetFamily(int _pFamily)
{
	addressInfo.ai_family = _pFamily;
}

void Networking::Client::SetProtocol(int _pProtocol)
{
	addressInfo.ai_protocol = _pProtocol;
}


// Send data to the server
int Networking::Client::Send(PCSTR _pSendBuffer)
{
	// Send the data to the server
	int bytesSent = send(connectionSocket,_pSendBuffer, strlen(_pSendBuffer),0 );

	// If there was an error, throw an exception
	if(bytesSent ==SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client send failed");
	}

	// Return  the number of bytes sent if the data was sent successfully
	return bytesSent;
}

// Send data to a specified address and port
int Networking::Client::SendTo(PCSTR _pBuffer, PCSTR _pAddress, int _pPort)
{
	// Create a sockaddr_in structure to hold the address and port of the recipient
	sockaddr_in recipient;

	// Zero out the sockaddr_in structure
	ZeroMemory(&recipient, sizeof(recipient));

	// Set the address family, port, and address of the recipient
	recipient.sin_family = AF_INET;
	recipient.sin_port = htons(_pPort);
	inet_pton(AF_INET, _pAddress, &recipient.sin_addr);

	// Send the data to the specified recipient
	int bytesSent = sendto(connectionSocket, _pBuffer, strlen(_pBuffer), 0, (sockaddr*)&recipient, sizeof(recipient));

	// If there was an error, throw an exception
	if(bytesSent == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client sendto failed");
	}

	// Return  the number of bytes sent if the data was sent successfully
	return bytesSent;
}


// Send a file to the server
void Networking::Client::SendFile(const std::string& _pFilePath)
{
	// Open the file for reading
	std::ifstream file(_pFilePath, std::ios::in | std::ios::binary);

	// Check if the file was opened successfully
	if(!file)
	{
        std::string errMsg = "Error: Unable to open file '" + _pFilePath + "'";
        Logger::getInstance().log(LogLevel::ERROR, errMsg);
		// Throw an exception if the file could not be opened
		throw std::runtime_error(errMsg);
	}

	// Read the file data into a buffer
	std::vector<char> fileData((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));

	// Send the file data to the server
	Send(&fileData[0]);
}


// Receive data from the server
std::vector <char> Networking::Client::Receive()
{
	// Initialize the number of bytes received to 0
	int bytesReceived =0;

	// Create a vector to store the received data
	std::vector<char> receiveBuffer;

	// Receive data from the server in a loop
	do{
		// Get the current size of the receive buffer
		int bufferStart = receiveBuffer.size();
		// Resize the buffer to make room for more data
		receiveBuffer.resize(bufferStart+512);
		// Receive data from the server
		bytesReceived = recv(connectionSocket, &receiveBuffer[bufferStart],512,0);
		// Resize the buffer to the actual size of the received data
		receiveBuffer.resize(bufferStart + bytesReceived);
	} while (bytesReceived == 512);

// If there was an error, throw an exception
	if (bytesReceived == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
    #endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client receive failed");
	}
	// Return the vector containing the received data
	return receiveBuffer;
}


// Receive data from a specified address and port
std::vector<char> Networking::Client::ReceiveFrom(PCSTR _pAddress, int _pPort)
{
	// Initialize the number of bytes received to 0
	int bytesReceived =0;

	// Create a sockaddr_in structure to hold the address and port of the sender
	sockaddr_in sender;

	// Zero out the sockaddr_in structure
	ZeroMemory(&sender, sizeof(sender));

	// Set the address family, port, and address of the sender
	sender.sin_family = AF_INET;
	sender.sin_port = htons(_pPort);
	inet_pton(AF_INET, _pAddress, &sender.sin_addr);

	// Receive data from the specified sender
	std::vector<char> receiveBuffer;


	// Receive data from the server in a loop
	do{
		// Get the current size of the receive buffer
		int bufferStart = receiveBuffer.size();
		// Resize the buffer to make room for more data
		receiveBuffer.resize(bufferStart+512);
		// Receive data from the server
		bytesReceived = recvfrom(connectionSocket, &receiveBuffer[bufferStart], 512, 0, (sockaddr*)&sender, (socklen_t*)sizeof(sender));
		// Resize the buffer to the actual size of the received data
		receiveBuffer.resize(bufferStart + bytesReceived);
	} while (bytesReceived == 512);


	// If there was an error, throw an exception
	if(bytesReceived == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client recvfrom failed");
	}

	// Return the received data
	return receiveBuffer;
}

// Receive a file from the server
void Networking::Client::ReceiveFile(const std::string& _pFilePath)
{
	// Open the file for writing
	std::ofstream file(_pFilePath, std::ios::out | std::ios::binary);

	// Check if the file was opened successfully
	if(!file)
	{
        std::string errMsg = "Error: Unable to open file '" + _pFilePath + "'";
        Logger::getInstance().log(LogLevel::ERROR, errMsg);
		// Throw an exception if the file could not be opened
		throw std::runtime_error(errMsg);
	}

	// Receive the file data from the server
	std::vector<char> fileData = Receive();

	// Write the file data to the file
	file.write(&fileData[0], fileData.size());
}


// Disconnect from the server and close the client socket
bool Networking::Client::Disconnect()
{
	// Disconnect from the server
	int errorCode = shutdown(connectionSocket, SHUT_RDWR);

	// If there was an error, throw an exception
	if(errorCode)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client disconnect failed (shutdown)");
	}

	// Close the client socket
	SOCKET tempSocket = connectionSocket; // Store for exception before invalidating
	errorCode = CLOSESOCKET(connectionSocket);

	// If there was an error, throw an exception
	if(errorCode)
	{
		// Get the error code
		int originalErrorCode = errorCode; // Store original error code from CLOSESOCKET
		errorCode = GETERROR(); // Get more detailed error if available

	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code, prefer more detailed one if available, else original
		throw Networking::NetworkException(tempSocket, errorCode ? errorCode : originalErrorCode, "Client disconnect failed (closesocket)");
	}
	clientIsConnected = false;
	// Return true if the client was disconnected and the socket was closed successfully
	return true;
}

//Returns whether the client is currently connected to a host.
bool Networking::Client::IsConnected()
{
	return clientIsConnected;
}

// Get the hostname of the client
std::string Networking::Client::GetHostName()
{
	// Get the hostname of the client
	char hostname[128];
	if(gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client gethostname failed");
	}

	// Return the hostname of the client
	return hostname;
}


// Get the hostname of the server
std::string Networking::Client::GetServerHostName()
{
	// Get the address info of the server
	addrinfo* serverAddressInfo = hostAddressInfo;

	// Get the hostname of the server
	char hostname[128];
	if(getnameinfo(serverAddressInfo->ai_addr, serverAddressInfo->ai_addrlen, hostname, sizeof(hostname), NULL, 0, 0) == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client getnameinfo failed for server hostname");
	}

	// Return the hostname of the server
	return hostname;
}

// Get the local IP address of the client
std::string Networking::Client::GetLocalIPAddress()
{
	// Get the local IP address of the client
	addrinfo* localAddress;
	if(getaddrinfo("localhost", NULL, NULL, &localAddress) == SOCKET_ERROR)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client getaddrinfo failed for local IP");
	}

	// Convert the local IP address to a string
	char localIP[128];
	if(inet_ntop(AF_INET, &((sockaddr_in*)localAddress->ai_addr)->sin_addr, localIP, sizeof(localIP)) == NULL)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client inet_ntop failed for local IP");
	}

	// Return the local IP address of the client
	return localIP;
}

// Get the remote IP address of the server
std::string Networking::Client::GetRemoteIPAddress()
{
	// Get the address info of the server
	addrinfo* serverAddressInfo = hostAddressInfo;

	// Convert the remote IP address to a string
	char remoteIP[128];
	if(inet_ntop(AF_INET, &((sockaddr_in*)serverAddressInfo->ai_addr)->sin_addr, remoteIP, sizeof(remoteIP)) == NULL)
	{
		// Get the error code
		int errorCode = GETERROR();

		// Close the socket
		CLOSESOCKET(connectionSocket);
	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Throw the error code
		throw Networking::NetworkException(connectionSocket, errorCode, "Client inet_ntop failed for remote IP");
	}

	// Return the remote IP address of the server
	return remoteIP;
}

// Connects to the server with retry logic.
bool Networking::Client::connectWithRetry(PCSTR _pHost, int _pPortNumber, int start_attempt /*= 0*/) {
    Logger::getInstance().log(LogLevel::INFO, "Attempting to connect to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber) + " with retries.");
    for (int attempt = start_attempt; attempt < Networking::kMaxRetries; ++attempt) {
        try {
            // Ensure WSAStartup is called. If WSAStartup has already been called, this call does nothing.
            // If a previous attempt failed and WSACleanup was called, this will reinitialize Winsock.
            if (!InitClientSocket()) { // InitClientSocket should return true on success
                 Logger::getInstance().log(LogLevel::ERROR, "WSAStartup failed during connectWithRetry attempt " + std::to_string(attempt + 1));
                 // If WSAStartup fails, retrying might not help unless it's a transient system issue.
                 // Consider a short sleep and retry or fail fast. For now, let it proceed to CreateClientTCPSocket which will also fail.
            }

            CreateClientTCPSocket(_pHost, _pPortNumber); // This sets up hostAddressInfo and creates the socket
            ConnectClientSocket(); // This uses hostAddressInfo to connect

            Logger::getInstance().log(LogLevel::INFO, "Successfully connected to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber) + " on attempt " + std::to_string(attempt + 1));
            // clientIsConnected = true; // ConnectClientSocket already sets this
            return true;
        } catch (const Networking::NetworkException& e) {
            Logger::getInstance().log(LogLevel::WARNING, "Connection attempt " + std::to_string(attempt + 1) + " of " + std::to_string(Networking::kMaxRetries) + " failed: " + e.what());
            
            // Cleanup is mostly handled by CreateClientTCPSocket and ConnectClientSocket on error.
            // Ensure socket is closed if it was created. connectionSocket might be invalid or closed already.
            // freeaddrinfo(hostAddressInfo) is called by CreateClientTCPSocket on error if hostAddressInfo was populated.
            // WSACleanup() is also called by CreateClientTCPSocket on error.

            if (attempt < Networking::kMaxRetries - 1) {
                long long backoff_ms = Networking::kBaseBackoffDelayMs * static_cast<long long>(std::pow(2, attempt));
                Logger::getInstance().log(LogLevel::INFO, "Retrying in " + std::to_string(backoff_ms) + " ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            } else {
                Logger::getInstance().log(LogLevel::ERROR, "Failed to connect to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber) + " after " + std::to_string(Networking::kMaxRetries) + " attempts.");
                return false; // Or throw e;
            }
        } catch (const std::exception& ex) { // Catch other potential std exceptions
            Logger::getInstance().log(LogLevel::ERROR, "An unexpected error occurred during connection attempt " + std::to_string(attempt + 1) + " of " + std::to_string(Networking::kMaxRetries) + ": " + ex.what());
            // Similar cleanup considerations as above.
            if (attempt < Networking::kMaxRetries - 1) {
                 long long backoff_ms = Networking::kBaseBackoffDelayMs * static_cast<long long>(std::pow(2, attempt));
                Logger::getInstance().log(LogLevel::INFO, "Retrying in " + std::to_string(backoff_ms) + " ms...");
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            } else {
                 Logger::getInstance().log(LogLevel::ERROR, "Failed to connect due to unexpected error after " + std::to_string(Networking::kMaxRetries) + " attempts.");
                return false; // Or throw;
            }
        }
    }
    return false; // All retries failed
}
