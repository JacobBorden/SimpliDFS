#include "utilities/client.h" // include the header file for the client class
#include "utilities/networkexception.h" // Include the NetworkException header
#include "utilities/logger.h" // Include the Logger header
#include <string>   // Required for std::to_string
#include <thread>         // For std::this_thread::sleep_for
#include <chrono>         // For std::chrono::seconds, std::chrono::milliseconds
#include <cmath>          // For std::pow
#include <iostream>       // For std::cout (verbose logging)
#include <iomanip>        // For std::put_time
#include <sstream>        // For std::ostringstream
#include <cstring>        // For memcpy
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h> // For htonl
#else
#include <arpa/inet.h> // For htonl
#endif

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
	try{
		// Initialize the client socket
		Networking::Client::InitClientSocket();
		// Create a TCP socket
		Networking::Client::CreateClientTCPSocket(_pHost, _pPortNumber);
		// Connect to the server
		Networking::Client::ConnectClientSocket();
        Logger::getInstance().log(LogLevel::INFO, "Client initialized and connected to " + std::string(_pHost) + ":" + std::to_string(_pPortNumber)); // Existing log
	}
	// Catch any exceptions that are thrown
	catch(const Networking::NetworkException& e) {
		// Print the error code
        Logger::getInstance().log(LogLevel::ERROR, "Exception thrown during Client construction with host/port: " + std::string(e.what())); // Existing log
		// std::cout<<"Exception thrown. Error Code "<<errorCode;
	}
    std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Client(host, port): Constructor Exit." << std::endl;
}

// Destructor for the client class
Networking::Client::~Client()
{
    // std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] ~Client(): Destructor Entry." << std::endl;
    // Add any specific cleanup logging if necessary, though IsConnected() check in Disconnect is primary for action.
    // std::cout << "[VERBOSE LOG Client " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] ~Client(): Destructor Exit." << std::endl;
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
int Networking::Client::Send(const char* _pSendBuffer, int length)
{
    if (!clientIsConnected) {
        // Logger::getInstance().log(LogLevel::WARN, "Client::Send: Attempting to send when not connected.");
        // throw Networking::NetworkException(connectionSocket, 0, "Client send failed: Not connected.");
        // Let the OS detect the error on send itself.
    }

    // 1. Prepare and send the length header
    uint32_t net_length = htonl(static_cast<uint32_t>(length));
    char header[4];
    memcpy(header, &net_length, sizeof(uint32_t));

    int headerBytesSent = send(connectionSocket, header, 4, 0);
    if (headerBytesSent == SOCKET_ERROR) {
        int errorCode = GETERROR();
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: send() failed for header with error: " + std::to_string(errorCode));
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(connectionSocket, errorCode, "Client send failed (header)");
    }
    if (headerBytesSent < 4) {
        // This case should ideally be handled by robust send loop, but for now, consider it a critical failure.
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: Incomplete header sent. Expected 4, sent " + std::to_string(headerBytesSent));
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(connectionSocket, 0, "Client send failed (incomplete header)");
    }

    // 2. Send the actual payload
    int payloadBytesSent = send(connectionSocket, _pSendBuffer, length, 0);
    if (payloadBytesSent == SOCKET_ERROR) {
        int errorCode = GETERROR();
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: send() failed for payload with error: " + std::to_string(errorCode));
        // Critical error: header might have been sent, but payload failed.
        clientIsConnected = false; // Mark as disconnected
        CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(connectionSocket, errorCode, "Client send failed (payload after header)");
    }
    if (payloadBytesSent < length) {
        // This case should ideally be handled by robust send loop for partial sends.
        // For now, consider it a critical failure as the receiver expects 'length' bytes.
        Logger::getInstance().log(LogLevel::ERROR, "Client::Send: Incomplete payload sent. Expected " + std::to_string(length) + ", sent " + std::to_string(payloadBytesSent));
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
    #ifdef _WIN32
        WSACleanup();
    #endif
        throw Networking::NetworkException(connectionSocket, 0, "Client send failed (incomplete payload)");
    }

    // Return the total number of bytes sent (header + payload)
    return headerBytesSent + payloadBytesSent;
}

// Send data to a specified address and port
int Networking::Client::SendTo(const char* _pBuffer, int length, PCSTR _pAddress, int _pPort)
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
	// NOTE: This SendTo for UDP does not currently implement the length header.
	// The subtask description focuses on TCP stream Send methods.
	// If UDP also needs length prefix, this would need similar modification.
	int bytesSent = sendto(connectionSocket, _pBuffer, length, 0, (sockaddr*)&recipient, sizeof(recipient));

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
    if (!fileData.empty()) {
        Send(fileData.data(), static_cast<int>(fileData.size()));
    } else {
        // Handle empty file case: send a zero-length message or specific protocol message
        // For now, sending a zero-length message.
        Send(nullptr, 0);
    }
}

// Helper function to read exactly n bytes from a socket
// Returns:
//   n          : if exactly n bytes were read successfully.
//   0 to n-1   : if EOF (peer closed connection) before n bytes were read.
//   -1         : on socket error (errno/WSAGetLastError() will be set by recv).
//   -2         : if interrupted by EINTR too many times.
static ssize_t read_n_bytes(SOCKET sock, char* buffer, size_t n, int max_eintr_retries = 5) {
    size_t bytes_read = 0;
    int eintr_retries = 0;
    while (bytes_read < n) {
        // On Windows, recv's length parameter is int, on POSIX it's size_t.
        // Safely cast n - bytes_read to int for Windows, ensuring it doesn't overflow int.
        // Max message size should be less than INT_MAX. Header is 4 bytes.
        int length_to_receive = static_cast<int>(n - bytes_read);
        if (length_to_receive == 0) break; // Should not happen if n > 0 and bytes_read < n

        ssize_t current_read = recv(sock, buffer + bytes_read, length_to_receive, 0);

        if (current_read == 0) { // Peer closed connection
            return bytes_read;
        }
        if (current_read < 0) { // Error
            int error_code = GETERROR();
            if (error_code == EINTR) {
                eintr_retries++;
                if (eintr_retries > max_eintr_retries) {
                    Logger::getInstance().log(LogLevel::WARN, "read_n_bytes: recv interrupted too many times.");
                    return -2;
                }
                continue;
            }
            // For blocking sockets, EAGAIN/EWOULDBLOCK are not expected.
            // Logger::getInstance().log(LogLevel::ERROR, "read_n_bytes: recv() error: " + std::to_string(error_code)); // Logged by caller
            return -1;
        }
        bytes_read += current_read;
    }
    return bytes_read;
}

// Receive data from the server
std::vector<char> Networking::Client::Receive() {
    if (!clientIsConnected) {
        Logger::getInstance().log(LogLevel::WARN, "Client::Receive: Attempting to receive when not connected.");
        // Or throw an exception, depending on desired behavior for this state.
        // For now, let it proceed and fail on recv, which is more indicative of the actual network state.
    }

    char header_buffer[4];
    ssize_t header_bytes_read = read_n_bytes(connectionSocket, header_buffer, 4);

    if (header_bytes_read == 0) { // Peer closed connection before sending anything (or after previous message)
        Logger::getInstance().log(LogLevel::INFO, "Client::Receive: Peer performed an orderly shutdown (0 bytes for header).");
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
        #ifdef _WIN32
        // WSACleanup(); // Consider if WSACleanup is appropriate here or at a higher level
        #endif
        return {}; // Return empty vector for graceful shutdown
    }
    if (header_bytes_read < 0 || header_bytes_read < 4) { // Socket error or incomplete header read
        int errorCode = (header_bytes_read < 0) ? GETERROR() : 0; // Get error if recv failed
        std::string msg = "Client::Receive: Failed to read message header. Bytes read: " + std::to_string(header_bytes_read);
        if (header_bytes_read < 0) msg += ", Error: " + std::to_string(errorCode);
        Logger::getInstance().log(LogLevel::ERROR, msg);
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        throw Networking::NetworkException(connectionSocket, errorCode, msg);
    }

    uint32_t payload_length_net;
    memcpy(&payload_length_net, header_buffer, sizeof(uint32_t));
    uint32_t payload_length = ntohl(payload_length_net);

    if (payload_length == 0) { // Valid zero-length message
        Logger::getInstance().log(LogLevel::DEBUG, "Client::Receive: Received zero-length message.");
        return {}; // Return empty vector
    }

    // Basic sanity check for payload_length to prevent absurd allocations.
    // Max typical message size might be 1MB or configurable. For now, a hardcoded limit.
    const uint32_t MAX_ALLOWED_PAYLOAD = 10 * 1024 * 1024; // 10MB, example limit
    if (payload_length > MAX_ALLOWED_PAYLOAD) {
        std::string msg = "Client::Receive: Payload length " + std::to_string(payload_length) + " exceeds maximum allowed (" + std::to_string(MAX_ALLOWED_PAYLOAD) + ").";
        Logger::getInstance().log(LogLevel::ERROR, msg);
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        throw Networking::NetworkException(connectionSocket, 0, msg); // Or a specific error code/exception type
    }


    std::vector<char> payload_buffer(payload_length);
    ssize_t payload_bytes_read = read_n_bytes(connectionSocket, payload_buffer.data(), payload_length);

    if (payload_bytes_read < 0 || static_cast<uint32_t>(payload_bytes_read) < payload_length) { // Socket error or incomplete payload
        int errorCode = (payload_bytes_read < 0) ? GETERROR() : 0;
        std::string msg = "Client::Receive: Failed to read full message payload. Expected: " + std::to_string(payload_length) + ", Got: " + std::to_string(payload_bytes_read);
        if (payload_bytes_read < 0) msg += ", Error: " + std::to_string(errorCode);
        else if (payload_bytes_read < static_cast<ssize_t>(payload_length)) msg += " (peer closed connection prematurely).";

        Logger::getInstance().log(LogLevel::ERROR, msg);
        clientIsConnected = false;
        CLOSESOCKET(connectionSocket);
        #ifdef _WIN32
        WSACleanup();
        #endif
        throw Networking::NetworkException(connectionSocket, errorCode, msg);
    }

    return payload_buffer;
}


// Receive data from a specified address and port
// Changed PCSTR _pAddress to const char* _pAddress for consistency, though not strictly required by task.
// The ReceiveFrom and SendTo methods using UDP are not the primary focus of this subtask (length prefixing for TCP).
// However, if length is passed to SendTo, its signature should match.
// For now, I am only changing the buffer type for consistency with Send.
// The original did not have a length parameter for ReceiveFrom, it reads until recvfrom returns less than buffer size.
std::vector<char> Networking::Client::ReceiveFrom(const char* _pAddress, int _pPort)
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
