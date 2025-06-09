#include "utilities/client.h" // include the header file for the client class
#include "utilities/networkexception.h" // Include the NetworkException header
#include "utilities/logger.h" // Include the Logger header
#include <string>   // Required for std::to_string
#include <sys/socket.h> // For SO_RCVTIMEO on POSIX
#include <thread>         // For std::this_thread::sleep_for
#include <chrono>         // For std::chrono::seconds, std::chrono::milliseconds
#include <cmath>          // For std::pow
#include <iostream>       // For std::cout (verbose logging)
#include <iomanip>        // For std::put_time
#include <sstream>        // For std::ostringstream
#include <openssl/ssl.h>
#include <openssl/err.h>

// Helper for timestamp logging in client.cpp and server.cpp
static std::string getNetworkTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    // oss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    // Using a simpler timestamp format for verbose network logs to reduce clutter if needed
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    // Convert to std::tm (broken down time)
    std::tm bt = *std::localtime(&t);
    oss << std::put_time(&bt, "%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}


namespace Networking {
    static const int kMaxRetries = 5;
    static const int kBaseBackoffDelayMs = 200; // Base delay for backoff in milliseconds
} // namespace Networking

// Constructor that initializes the client socket
Networking::Client::Client()
{
    std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Client(): Constructor Entry." << std::endl;
	try {
		// Initialize the client socket
		Networking::Client::InitClientSocket();
        Logger::getInstance().log(LogLevel::INFO, "Client initialized."); // Existing log
	}

	// Catch any exceptions that are thrown
	catch(const Networking::NetworkException& e) {
		// Print the error code
        Logger::getInstance().log(LogLevel::ERROR, "Exception thrown during Client construction: " + std::string(e.what())); // Existing log
		// std::cout<<"Exception thrown. Error Code"<<errorCode;
	}
    std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Client(): Constructor Exit." << std::endl;
}

// Constructor that initializes the client socket and connects to the server
Networking::Client::Client(PCSTR _pHost, int _pPortNumber)
{
    std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Client(host, port): Constructor Entry. Host: " << _pHost << " Port: " << _pPortNumber << std::endl;
    try {
        // Initialize the client socket
        Networking::Client::InitClientSocket();
        // Create a TCP socket
        Networking::Client::CreateClientTCPSocket(_pHost, _pPortNumber);
        // Connect to the server
        Networking::Client::ConnectClientSocket();
        Logger::getInstance().log(LogLevel::INFO, "Client initialized and connected to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber));
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "Exception thrown during Client construction with host/port: " + std::string(e.what()));
        throw;
    }
    std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Client(host, port): Constructor Exit." << std::endl;
}

// Destructor for the client class
Networking::Client::~Client()
{
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = nullptr;
    }
    if (sslCtx) {
        SSL_CTX_free(sslCtx);
        sslCtx = nullptr;
    }
    if (IsConnected()) {
        Disconnect();
    }
}

// InitClientSocket - No changes requested beyond constructor context logs
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
        throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client TCP socket creation failed (getaddrinfo)");
    #else
        throw Networking::NetworkException(-1, errorCode, "Client TCP socket creation failed (getaddrinfo)");
    #endif
    }

    connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

    if(INVALIDSOCKET(connectionSocket))
    {
        int socketErrorCode = GETERROR(); // Renamed to avoid conflict with outer errorCode
    #ifdef _WIN32
        WSACleanup();
    #endif
        // Pass the actual failing socket descriptor
        throw Networking::NetworkException(connectionSocket, socketErrorCode, "Client TCP socket creation failed (socket)");
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
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client UDP socket creation failed (getaddrinfo)");
    #else
		throw Networking::NetworkException(-1, errorCode, "Client UDP socket creation failed (getaddrinfo)");
    #endif
	}

	// Create the UDP socket
	connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

	// If the socket is invalid, throw an exception
	if(INVALIDSOCKET(connectionSocket))
	{
		// Get the error code
		int socketErrorCode = GETERROR(); // Renamed

	#ifdef _WIN32
		// Clean up the Windows Sockets DLL
		WSACleanup();
	#endif
		// Pass the actual failing socket descriptor
		throw Networking::NetworkException(connectionSocket, socketErrorCode, "Client UDP socket creation failed (socket)");
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
		throw Networking::NetworkException(INVALID_SOCKET, errorCode, "Client socket creation failed (getaddrinfo)");
    #else
		throw Networking::NetworkException(-1, errorCode, "Client socket creation failed (getaddrinfo)");
    #endif
	}

	connectionSocket = socket(hostAddressInfo->ai_family, hostAddressInfo->ai_socktype,  hostAddressInfo->ai_protocol);

	if(INVALIDSOCKET(connectionSocket))
	{
		int socketErrorCode = GETERROR(); // Renamed
	#ifdef _WIN32
		WSACleanup();
	#endif
        // Pass the actual failing socket descriptor
		throw Networking::NetworkException(connectionSocket, socketErrorCode, "Client socket creation failed (socket)");
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
        CLOSESOCKET(connectionSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        clientIsConnected = false;
#ifdef ECONNREFUSED
        std::string errName = (errorCode == ECONNREFUSED) ? "ECONNREFUSED" : std::to_string(errorCode);
#else
        std::string errName = std::to_string(errorCode);
#endif
        throw std::runtime_error("Client connect failed: " + errName + " (" + std::string(strerror(errorCode)) + ")");
    }
    clientIsConnected = true;

    if (useTLS) {
        ssl = SSL_new(sslCtx);
        SSL_set_fd(ssl, connectionSocket);
        if (SSL_connect(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            clientIsConnected = false;
            CLOSESOCKET(connectionSocket);
            SSL_free(ssl);
            ssl = nullptr;
            throw std::runtime_error("TLS handshake failed");
        }
    }

    // Set receive timeout for the connected socket
    #ifdef _WIN32
        DWORD timeout = 5000; // 5 seconds in milliseconds
        if (setsockopt(connectionSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout) < 0) {
            Logger::getInstance().log(LogLevel::WARN, "Failed to set SO_RCVTIMEO on client socket: " + std::to_string(WSAGetLastError()));
            // Not failing fatally, connection is up, but receives might block indefinitely.
        } else {
            Logger::getInstance().log(LogLevel::DEBUG, "Successfully set SO_RCVTIMEO to 5 seconds on client socket.");
        }
    #else // POSIX
        struct timeval tv;
        tv.tv_sec = 5;  // 5 seconds timeout
        tv.tv_usec = 0;
        if (setsockopt(connectionSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
            Logger::getInstance().log(LogLevel::WARN, "Failed to set SO_RCVTIMEO on client socket: " + std::string(strerror(errno)));
            // Not failing fatally.
        } else {
            Logger::getInstance().log(LogLevel::DEBUG, "Successfully set SO_RCVTIMEO to 5 seconds on client socket.");
        }
    #endif

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


// Helper function to send all data in a buffer
static bool send_all(SOCKET sock, const char* buffer, size_t length,
                     bool useTLS, SSL* ssl, bool& clientIsConnected_ref) {
    size_t totalBytesSent = 0;
    while (totalBytesSent < length) {
        int bytesSent = useTLS ?
            SSL_write(ssl, buffer + totalBytesSent, length - totalBytesSent) :
            send(sock, buffer + totalBytesSent, length - totalBytesSent, 0);
        if (bytesSent == SOCKET_ERROR) {
            int errorCode = GETERROR();
            Logger::getInstance().log(LogLevel::ERROR, "send_all: send() failed with error: " + std::to_string(errorCode));
            clientIsConnected_ref = false; // Update connection state
            CLOSESOCKET(sock); // Consider if WSACleanup is needed here too per original pattern
            #ifdef _WIN32
            WSACleanup();
            #endif
            throw Networking::NetworkException(sock, errorCode, "send_all failed");
        }
        if (bytesSent == 0) { // Should not happen with blocking sockets unless length is 0
            Logger::getInstance().log(LogLevel::ERROR, "send_all: send() returned 0, treating as error.");
            clientIsConnected_ref = false;
            CLOSESOCKET(sock);
            #ifdef _WIN32
            WSACleanup();
            #endif
            throw Networking::NetworkException(sock, 0, "send_all failed: sent 0 bytes");
        }
        totalBytesSent += bytesSent;
    }
    return true;
}

// Send data to the server (modified for length-prefixing)
int Networking::Client::Send(const std::string& sendBuffer)
{
    if (!clientIsConnected || INVALIDSOCKET(connectionSocket)) {
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: Attempting to send when not connected.");
        return -1;
    }

    size_t payloadLength = sendBuffer.length(); // Use std::string::length()
    uint32_t netPayloadLength = htonl(static_cast<uint32_t>(payloadLength));

    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Send] Sending header: payloadLength = " + std::to_string(payloadLength));

    // Send the header (payload length)
    try {
        send_all(connectionSocket, reinterpret_cast<const char*>(&netPayloadLength),
                 sizeof(netPayloadLength), useTLS, ssl, this->clientIsConnected);
    } catch (const Networking::NetworkException& e) {
        // clientIsConnected and socket cleanup is handled by send_all
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: Failed to send header: " + std::string(e.what()));
        throw; // Re-throw to inform caller
    }

    // Log the payload carefully, as it might contain non-printable characters or be very long.
    // For debugging, one might log a snippet or indicate its presence without printing fully.
    // Here, we'll use the existing pattern but be mindful it might be truncated by logger or terminal if very large/binary.
    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Send] Header sent. Sending payload (size: " + std::to_string(payloadLength) + ")");


    // Send the actual payload
    if (payloadLength > 0) {
        try {
            send_all(connectionSocket, sendBuffer.data(), payloadLength, // Use std::string::data()
                     useTLS, ssl, this->clientIsConnected);
        } catch (const Networking::NetworkException& e) {
            // clientIsConnected and socket cleanup is handled by send_all
            Logger::getInstance().log(LogLevel::ERROR, "Client::Send: Failed to send payload: " + std::string(e.what()));
            throw; // Re-throw
        }
    }
    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Send] Payload sent successfully.");
	return static_cast<int>(payloadLength); // Return payload length, consistent with original "bytesSent" intent
}

// Send data to a specified address and port
int Networking::Client::SendTo(PCSTR _pBuffer, PCSTR _pAddress, int _pPort)
{
        if (!clientIsConnected || INVALIDSOCKET(connectionSocket)) {
                Logger::getInstance().log(LogLevel::ERROR, "Client::SendTo: Attempting to send when not connected.");
                return -1;
        }
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


// Helper function to receive exactly 'length' bytes
static bool recv_all(SOCKET sock, char* buffer, size_t length,
                     bool useTLS, SSL* ssl, bool& clientIsConnected_ref) {
    size_t totalBytesReceived = 0;
    while (totalBytesReceived < length) {
        int bytesReceived = useTLS ?
            SSL_read(ssl, buffer + totalBytesReceived, length - totalBytesReceived) :
            recv(sock, buffer + totalBytesReceived, length - totalBytesReceived, 0);
        if (bytesReceived == 0) { // Graceful shutdown by peer
            Logger::getInstance().log(LogLevel::INFO, "recv_all: Peer has performed an orderly shutdown during message reception.");
            clientIsConnected_ref = false;
            CLOSESOCKET(sock);
            #ifdef _WIN32
            WSACleanup();
            #endif
            // This is an unexpected EOF if we were expecting more data for the current message.
            throw Networking::NetworkException(sock, 0, "recv_all failed: Peer shutdown prematurely");
        }
        if (bytesReceived == SOCKET_ERROR) {
            int errorCode = GETERROR();
            std::string logMessagePrefix = "recv_all: recv() ";
            bool isTimeout = false;

            #ifdef _WIN32
            if (errorCode == WSAETIMEDOUT) {
                logMessagePrefix += "timed out (WSAETIMEDOUT). Error: ";
                isTimeout = true;
                Logger::getInstance().log(LogLevel::WARN, logMessagePrefix + std::to_string(errorCode));
            } else {
                logMessagePrefix += "failed with error: ";
                Logger::getInstance().log(LogLevel::ERROR, logMessagePrefix + std::to_string(errorCode));
            }
            // Per subtask: "Let's maintain this behavior for timeouts as well for now" (close socket, set not connected)
            clientIsConnected_ref = false;
            CLOSESOCKET(sock);
            WSACleanup(); // Cleanup Winsock on any error
            #else // POSIX
            if (errorCode == EAGAIN || errorCode == EWOULDBLOCK) {
                logMessagePrefix += "timed out (EAGAIN/EWOULDBLOCK). Error: ";
                isTimeout = true;
                Logger::getInstance().log(LogLevel::WARN, logMessagePrefix + std::to_string(errorCode) + " (" + strerror(errorCode) + ")");
            } else {
                logMessagePrefix += "failed with error: ";
                Logger::getInstance().log(LogLevel::ERROR, logMessagePrefix + std::to_string(errorCode) + " (" + strerror(errorCode) + ")");
            }
            // Per subtask: "Let's maintain this behavior for timeouts as well for now"
            clientIsConnected_ref = false;
            CLOSESOCKET(sock); // Close socket on any error
            #endif

            // Throw exception, indicating timeout in message if applicable
            std::string exceptionMessage = "recv_all failed";
            if (isTimeout) {
                exceptionMessage += " due to timeout";
            }
            throw Networking::NetworkException(sock, errorCode, exceptionMessage);
        }
        totalBytesReceived += bytesReceived;
    }
    return true;
}


// Receive data from the server (modified for length-prefixing)
std::vector<char> Networking::Client::Receive() {
    if (!clientIsConnected) {
        Logger::getInstance().log(LogLevel::WARN, "Client::Receive: Attempting to receive when not connected.");
        // Allowing the attempt to proceed and fail via OS, or throw early:
        // throw Networking::NetworkException(connectionSocket, ENOTCONN, "Client receive failed: Not connected.");
    }

    uint32_t netPayloadLength;
    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Receive] Receiving header (4 bytes).");
    try {
        recv_all(connectionSocket, reinterpret_cast<char*>(&netPayloadLength),
                 sizeof(netPayloadLength), useTLS, ssl, this->clientIsConnected);
    } catch (const Networking::NetworkException& e) {
        // clientIsConnected and socket cleanup is handled by recv_all
        // If it was due to peer shutdown (bytesReceived == 0 in recv_all), it's an EOF before getting full header.
        if (e.GetErrorCode() == 0) { // Our custom indication of peer shutdown from recv_all
             Logger::getInstance().log(LogLevel::INFO, "Client::Receive: Peer shutdown while trying to read header.");
        } else {
             Logger::getInstance().log(LogLevel::ERROR, "Client::Receive: Failed to receive header: " + std::string(e.what()));
        }
        // If client is no longer connected due to this error, return empty vector.
        // This is a deviation from throwing for other errors, but necessary if EOF is to mean "no message".
        // However, an EOF during header read is more like an error/incomplete message.
        // For this subtask's tests, if a zero-length message is an empty send, then an EOF before header is an error.
        // If the test expects an empty vector on EOF before header, that's different.
        // Assuming robust handling: EOF before full header is an error or leads to empty vector if connection is now closed.
        if (!clientIsConnected) return {}; // Return empty if connection dropped
        throw; // Otherwise, re-throw for other recv_all errors.
    }

    uint32_t payloadLength = ntohl(netPayloadLength);
    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Receive] Header received. Payload length = " + std::to_string(payloadLength));

    if (payloadLength == 0) {
        Logger::getInstance().log(LogLevel::DEBUG, "[Client::Receive] Zero-length payload indicated. Returning empty vector.");
        return {}; // Correctly handle zero-length message
    }

    std::vector<char> payloadBuffer(payloadLength);
    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Receive] Receiving payload (" + std::to_string(payloadLength) + " bytes).");
    try {
        recv_all(connectionSocket, payloadBuffer.data(), payloadLength,
                 useTLS, ssl, this->clientIsConnected);
    } catch (const Networking::NetworkException& e) {
        // clientIsConnected and socket cleanup handled by recv_all
        Logger::getInstance().log(LogLevel::ERROR, "Client::Receive: Failed to receive payload: " + std::string(e.what()));
        if (!clientIsConnected) return {}; // Return empty if connection dropped mid-payload
        throw; // Re-throw for other recv_all errors
    }

    Logger::getInstance().log(LogLevel::DEBUG, "[Client::Receive] Payload received successfully.");
    return payloadBuffer;
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
        int result = shutdown(connectionSocket, SHUT_RDWR);
        if(result == SOCKET_ERROR)
        {
                int shutdownErr = GETERROR();
                #ifdef _WIN32
                if(shutdownErr != WSAENOTCONN)
                #else
                if(shutdownErr != ENOTCONN)
                #endif
                {
                        Logger::getInstance().log(LogLevel::WARN, "Client shutdown failed: " + std::to_string(shutdownErr));
                }
        }

        SOCKET tmp = connectionSocket;
        result = CLOSESOCKET(connectionSocket);
        if(result == SOCKET_ERROR)
        {
                int closeErr = GETERROR();
                Logger::getInstance().log(LogLevel::WARN, "Client close failed: " + std::to_string(closeErr));
        }
#ifdef _WIN32
        WSACleanup();
#endif
        clientIsConnected = false;
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
            Logger::getInstance().log(LogLevel::WARN, "Connection attempt " + std::to_string(attempt + 1) + " of " + std::to_string(Networking::kMaxRetries) + " failed: " + e.what());
            
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

bool Networking::Client::enableTLS(const std::string& certFile,
                                   const std::string& keyFile,
                                   const std::string& caFile) {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    const SSL_METHOD* method = TLS_client_method();
    sslCtx = SSL_CTX_new(method);
    if (!sslCtx) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    if (!SSL_CTX_use_certificate_file(sslCtx, certFile.c_str(), SSL_FILETYPE_PEM)) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    if (!SSL_CTX_use_PrivateKey_file(sslCtx, keyFile.c_str(), SSL_FILETYPE_PEM)) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    if (!caFile.empty()) {
        if (!SSL_CTX_load_verify_locations(sslCtx, caFile.c_str(), nullptr)) {
            ERR_print_errors_fp(stderr);
            return false;
        }
    }
    useTLS = true;
    return true;
}
