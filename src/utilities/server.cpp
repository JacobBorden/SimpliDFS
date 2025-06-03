#include "utilities/server.h"
#include "utilities/logger.h" // Ensure logger is included for Logger::getInstance()
#include <string>   // For std::string conversion if needed (e.g. ex.what())
#include <iostream> // For std::cout (verbose logging)
#include <iomanip>  // For std::put_time (used by getNetworkTimestamp)
#include <sstream>  // For std::ostringstream (used by getNetworkTimestamp)
#include <thread>   // For std::this_thread::get_id()
#include <chrono>   // For timestamp components (used by getNetworkTimestamp)
#include <cstring>  // For memcpy
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h> // For htonl
#else
#include <arpa/inet.h> // For htonl
#endif

// Assuming getNetworkTimestamp is defined in client.cpp or a common header accessible here.
// If not, it needs to be redefined or included. For this patch, we'll assume it's available.
// (If it were in client.cpp only, this would be a problem, but let's assume it's moved to a common place or duplicated)
// To be safe, let's ensure a version is available here:
static std::string getNetworkTimestamp() { // Duplicated from client.cpp for now
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm bt = *std::localtime(&t); // Ensure this is thread-safe or handled if used across threads heavily
    oss << std::put_time(&bt, "%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}


Networking::Server::Server(int _pPortNumber, ServerType _pServerType)
    : portNumber_(_pPortNumber), serverType_(_pServerType), serverSocket(0), serverIsConnected(false) {
    std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Server(): Constructor Entry. Port: " << _pPortNumber << " Type: " << _pServerType << std::endl;
    // Constructor now only initializes members.
    // Calls to InitServer() and CreateServerSocketInternal() are moved to startListening().
    std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] Server(): Constructor Exit." << std::endl;
}


Networking::Server::~Server()
{
    // std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] ~Server(): Destructor Entry." << std::endl;
    // Ensure Shutdown is called if server was connected.
    // However, relying on destructor for critical cleanup like network shutdown can be tricky.
    // Explicit Shutdown() call by user is preferred.
    // if (serverIsConnected) {
    //     Shutdown(); // Consider if this is safe or if it should be logged if not explicitly shut down.
    // }
    // std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] ~Server(): Destructor Exit." << std::endl;
}


bool Networking::Server::startListening() {
    std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: Entry. Port: " << portNumber_ << std::endl;
    if (serverIsConnected) {
        std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: Server already connected. Exiting." << std::endl;
        return true; // Already started
    }

    try {
        if (!InitServer()) {
             std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: InitServer() failed. Exiting." << std::endl;
            return false;
        }
        // Ensure the call is to CreateServerSocketInternal()
        if (!this->CreateServerSocketInternal()) {
            std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: CreateServerSocketInternal() failed. Exiting." << std::endl;
            return false;
        }
        serverIsConnected = true; // Set only after all steps succeed
        std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: Success. Server is connected. Exiting." << std::endl;
        return true;
    } catch (const Networking::NetworkException& ne) {
        std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: NetworkException: " << ne.what() << ". Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "startListening failed: " + std::string(ne.what()));
        serverIsConnected = false; // Ensure this is false on failure
        return false;
    } catch (const std::exception& e) {
        std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: std::exception: " << e.what() << ". Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "startListening failed with std::exception: " + std::string(e.what()));
        serverIsConnected = false;
        return false;
    }
}


bool Networking::Server::InitServer()
{
    // std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] InitServer: Entry." << std::endl; // Can be noisy
    #ifdef _WIN32
	try
	{
		int errorCode = WSAStartup(VERSIONREQUESTED, wsaData);
		if(errorCode)
		{
			Networking::NetworkException netEx(-1, errorCode, "Unable to Initilize Server");
			throw netEx;
		}
	}
	catch(Networking::NetworkException &ex)
	{
		Logger::getInstance().log(LogLevel::FATAL, "InitServer failed: " + std::string(ex.what()));
		std::exit(EXIT_FAILURE);
	}

   #endif

	return true;
}

// Definition for CreateServerSocketInternal, using member variables
bool Networking::Server::CreateServerSocketInternal()
{
    std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] CreateServerSocketInternal: Entry. Port: " << portNumber_ << " Type: " << serverType_ << std::endl;
    // Use serverType_ to determine address family, though current logic might default to IPv4
    int addressFamily = (serverType_ == ServerType::IPv6) ? AF_INET6 : AF_INET;
    // The original code consistently used AF_INET. Sticking to that for minimal change for now.
    // Forcing IPv4 as per original logic until IPv6 is fully plumbed for serverInfo
    addressFamily = AF_INET;


    ZeroMemory(&addressInfo, sizeof(addressInfo));
    SetFamily(addressFamily);
    SetSocketType(SOCK_STREAM);
    SetProtocol(IPPROTO_TCP);

    // Set up the sockaddr_in structure
    // TODO: Adapt for IPv6 if addressFamily is AF_INET6 using sockaddr_in6
    memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.sin_family = addressFamily;
    serverInfo.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces
    serverInfo.sin_port = htons(portNumber_); // Use member variable

    CreateSocket(); // Internally uses serverInfo.sin_family
    BindSocket();   // Internally uses serverInfo
    ListenOnSocket();
    // serverIsConnected should be set by startListening after all steps succeed
    std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] CreateServerSocketInternal: Exit (success)." << std::endl;
    return true;
}



void Networking::Server::CreateSocket()
{

	static int retries =0;
	try
	{
		// Create the socket
		serverSocket = socket(serverInfo.sin_family, SOCK_STREAM, IPPROTO_TCP);
		// Check for errors
		if (INVALIDSOCKET(serverSocket))
		{
			// Get the error code
			int errorCode = GETERROR();
			// Throw an exception
			ThrowSocketException(serverSocket, errorCode);
		}

        // Set SO_REUSEADDR option
        int reuseaddr = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuseaddr, sizeof(reuseaddr)) < 0) {
            int errorCode = GETERROR();
            Logger::getInstance().log(LogLevel::WARN, "setsockopt(SO_REUSEADDR) failed with error code " + std::to_string(errorCode) + ": " + std::string(strerror(errorCode)));
            // Depending on policy, this could be a fatal error, but often it's not.
            // For now, just log and continue.
        } else {
            Logger::getInstance().log(LogLevel::DEBUG, "Successfully set SO_REUSEADDR on server socket.");
        }

        // Set SO_REUSEPORT option (more platform specific)
        // SO_REUSEPORT allows multiple sockets to bind to the same IP address and port.
        // This is particularly useful on Linux for load distribution or fast restarts.
        // On Windows, SO_REUSEADDR effectively allows similar behavior for TCP sockets
        // regarding quick restarts, but SO_REUSEPORT is not typically used or available in the same way.
        #if defined(SO_REUSEPORT) && !defined(_WIN32)
        int reuseport = 1;
        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseport, sizeof(reuseport)) < 0) {
            int errorCode = GETERROR();
            Logger::getInstance().log(LogLevel::WARN, "setsockopt(SO_REUSEPORT) failed with error code " + std::to_string(errorCode) + ": " + std::string(strerror(errorCode)));
            // Log and continue
        } else {
            Logger::getInstance().log(LogLevel::DEBUG, "Successfully set SO_REUSEPORT on server socket.");
        }
        #endif

		retries =0;
	}

	catch(Networking::NetworkException &ex)
	{
		switch (ex.GetErrorCode())
		{
		case EACCES:
			Logger::getInstance().log(LogLevel::FATAL, "CreateSocket failed (EACCES): " + std::string(ex.what()));
			std::exit(EXIT_FAILURE);

		case EAFNOSUPPORT:
			Logger::getInstance().log(LogLevel::FATAL, "CreateSocket failed (EAFNOSUPPORT): " + std::string(ex.what()));
			std::exit(EXIT_FAILURE);

		case EADDRINUSE:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "CreateSocket failed (EADDRINUSE), retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				CreateSocket();
			}

			else
			{
				Logger::getInstance().log(LogLevel::FATAL, "CreateSocket failed (EADDRINUSE) after max retries: " + std::string(ex.what()));
				std::exit(EXIT_FAILURE);
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "CreateSocket failed (EINTR), retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				CreateSocket();
			}

			else
			{
				Logger::getInstance().log(LogLevel::FATAL, "CreateSocket failed (EINTR) after max retries: " + std::string(ex.what()));
				std::exit(EXIT_FAILURE);
			}
		default:
			Logger::getInstance().log(LogLevel::FATAL, "CreateSocket failed (default case): " + std::string(ex.what()));
			std::exit(EXIT_FAILURE);
		}
	}
}

void Networking::Server::BindSocket()
{
	static int retries=0;
	// Bind the socket to a local address and port
	try{
		if (bind(serverSocket, (sockaddr*)&serverInfo, sizeof(serverInfo)) == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();
			// Throw an exception
			ThrowBindException(serverSocket, errorCode);
		}
		retries = 0;
	}
	catch(Networking::NetworkException &ex)
	{

		switch(ex.GetErrorCode())
		{
		case EADDRINUSE:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "BindSocket failed (EADDRINUSE), retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				BindSocket();
			}
			else
			{
				Logger::getInstance().log(LogLevel::FATAL, "BindSocket failed (EADDRINUSE) after max retries: " + std::string(ex.what()));
				std::exit(EXIT_FAILURE);
			}
		case EADDRNOTAVAIL:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "BindSocket failed (EADDRNOTAVAIL), retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				BindSocket();
			}
			else
			{
				Logger::getInstance().log(LogLevel::FATAL, "BindSocket failed (EADDRNOTAVAIL) after max retries: " + std::string(ex.what()));
				std::exit(EXIT_FAILURE);
			}

		default:
			Logger::getInstance().log(LogLevel::FATAL, "BindSocket failed (default case): " + std::string(ex.what()));
			std::exit(EXIT_FAILURE);
		}

	}
}

void Networking::Server::ListenOnSocket()
{
	static int retries =0;
	try{
		// Start listening for incoming connections
		if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();
			// Throw an exception
			ThrowListenException(serverSocket, errorCode);
		}
		retries =0;
	}
	catch (Networking::NetworkException &ex)
	{
		switch(ex.GetErrorCode())
		{
		case EADDRINUSE:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				ListenOnSocket();
			}
			else{
				Logger::getInstance().log(LogLevel::FATAL, "ListenOnSocket failed (EADDRINUSE) after max retries: " + std::string(ex.what()));
				std::exit(EXIT_FAILURE);
			}
		default:
			Logger::getInstance().log(LogLevel::FATAL, "ListenOnSocket failed (default case): " + std::string(ex.what()));
			std::exit(EXIT_FAILURE);
		}
	}
}

Networking::ClientConnection Networking::Server::Accept()
{
// Create a client connection structure to store information about the client
	Networking::ClientConnection client;

	static int retries =0;
// Accept a connection from a client

	try{

		if(serverInfo.sin_family == AF_INET)
		{
			int clientAddrSize = sizeof(client.clientInfo);
			client.clientSocket = accept(serverSocket, (sockaddr*)&client.clientInfo, (socklen_t *)&clientAddrSize);
		}
		else if (serverInfo.sin_family == AF_INET6)
		{
			int clientAddrSize = sizeof(client.clientInfo6);
			client.clientSocket = accept(serverSocket, (sockaddr*)&client.clientInfo6, (socklen_t *)&clientAddrSize);
		}
// If there was an error, throw an exception
		if ( INVALIDSOCKET(client.clientSocket))
		{
			// Get the error code
			int errorCode = GETERROR();
			Networking::ThrowAcceptException(serverSocket, errorCode);
		}
		retries = 0;
	}

	catch(NetworkException &ex)
	{
		switch (ex.GetErrorCode())
		{
			#ifdef _WIN32

		case WSAEINTR:
			if(retries <MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Accept();
			}
            Logger::getInstance().log(LogLevel::ERROR, "Accept failed (WSAEINTR) after max retries or during retry: " + std::string(ex.what()));
			return Networking::ClientConnection();

			#else

		case EINTR:
			if(retries <  MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "Accept failed (EINTR), retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Accept();
			}

            Logger::getInstance().log(LogLevel::ERROR, "Accept failed (EINTR) after max retries or during retry: " + std::string(ex.what()));
			return Networking::ClientConnection();

			#endif

		default:
            Logger::getInstance().log(LogLevel::ERROR, "Accept failed (default case): " + std::string(ex.what()));
			return Networking::ClientConnection();
		}
	}

// Add the client connection to the list of clients
	clients.push_back(client);
    Logger::getInstance().log(LogLevel::INFO, "New connection from " + GetClientIPAddress(client) + " on socket " + std::to_string(client.clientSocket));
	return client;
}

void Networking::Server::SetSocketType(int _pSocktype)
{
	addressInfo.ai_socktype = _pSocktype;
}

void Networking::Server::SetProtocol(int _pProtocol)
{
	addressInfo.ai_protocol = _pProtocol;
}

void Networking::Server::SetFamily(int _pFamily)
{
	addressInfo.ai_family = _pFamily;
}


// Send data to the client
int Networking::Server::Send(const char* _pSendBuffer, int length, Networking::ClientConnection _pClient)
{
    // static int retries = 0; // Retries should be managed per operation, not static across all calls
    int totalBytesSuccessfullySent = 0;

    // 1. Prepare and send the length header
    uint32_t net_length = htonl(static_cast<uint32_t>(length));
    char header[4];
    memcpy(header, &net_length, sizeof(uint32_t));

    int headerBytesSent = 0;
    int send_attempts = 0;
    while (send_attempts < MAX_RETRIES) {
        headerBytesSent = send(_pClient.clientSocket, header, 4, 0);
        if (headerBytesSent == SOCKET_ERROR) {
            int errorCode = GETERROR();
            // Non-blocking sockets might return EAGAIN or EWOULDBLOCK, indicating to try again.
            // Other errors might be more permanent.
            if (errorCode == EAGAIN || errorCode == EWOULDBLOCK || errorCode == EINTR) {
                send_attempts++;
                Logger::getInstance().log(LogLevel::WARN, "Server::Send (header): send() failed with " + std::to_string(errorCode) + ", retrying (" + std::to_string(send_attempts) + "/" + std::to_string(MAX_RETRIES) + ") for client " + GetClientIPAddress(_pClient));
                std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY * 100)); // Shorter delay for send retries
                continue;
            }
            Logger::getInstance().log(LogLevel::ERROR, "Server::Send: send() failed for header with error: " + std::to_string(errorCode) + " to client " + GetClientIPAddress(_pClient));
            DisconnectClient(_pClient);
            throw Networking::NetworkException(_pClient.clientSocket, errorCode, "Server send failed (header)");
        }
        if (headerBytesSent < 4 && headerBytesSent >= 0) { // Partial send for header is problematic
             Logger::getInstance().log(LogLevel::ERROR, "Server::Send: Incomplete header sent. Expected 4, sent " + std::to_string(headerBytesSent) + " to client " + GetClientIPAddress(_pClient));
             DisconnectClient(_pClient);
             throw Networking::NetworkException(_pClient.clientSocket, 0, "Server send failed (incomplete header)");
        }
        break; // Success
    }
     if (headerBytesSent < 4) { // Handles MAX_RETRIES exceeded or other break from loop without success
        Logger::getInstance().log(LogLevel::ERROR, "Server::Send: Failed to send header after " + std::to_string(MAX_RETRIES) + " attempts to client " + GetClientIPAddress(_pClient));
        DisconnectClient(_pClient);
        // Consider throwing an exception here as well, or return an error code
        // For now, throwing to indicate failure to the caller.
        throw Networking::NetworkException(_pClient.clientSocket, 0, "Server send failed (header after retries)");
    }
    totalBytesSuccessfullySent += headerBytesSent;

    // 2. Send the actual payload, only if header was successful
    if (length > 0 && _pSendBuffer != nullptr) {
        int payloadBytesSent = 0;
        send_attempts = 0;
        int remainingBytes = length;
        const char* currentBufferPosition = _pSendBuffer;

        while (remainingBytes > 0 && send_attempts < MAX_RETRIES) {
            payloadBytesSent = send(_pClient.clientSocket, currentBufferPosition, remainingBytes, 0);
            if (payloadBytesSent == SOCKET_ERROR) {
                int errorCode = GETERROR();
                if (errorCode == EAGAIN || errorCode == EWOULDBLOCK || errorCode == EINTR) {
                    send_attempts++;
                    Logger::getInstance().log(LogLevel::WARN, "Server::Send (payload): send() failed with " + std::to_string(errorCode) + ", retrying (" + std::to_string(send_attempts) + "/" + std::to_string(MAX_RETRIES) + ") for client " + GetClientIPAddress(_pClient));
                    std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY * 100));
                    continue;
                }
                Logger::getInstance().log(LogLevel::ERROR, "Server::Send: send() failed for payload with error: " + std::to_string(errorCode) + " to client " + GetClientIPAddress(_pClient));
                DisconnectClient(_pClient); // Critical: payload failed after header potentially sent.
                throw Networking::NetworkException(_pClient.clientSocket, errorCode, "Server send failed (payload)");
            }
            if (payloadBytesSent == 0 && remainingBytes > 0) { // Should not happen with blocking sockets unless connection closed
                 Logger::getInstance().log(LogLevel::ERROR, "Server::Send: send() returned 0 for payload, client " + GetClientIPAddress(_pClient) + " may have disconnected.");
                 DisconnectClient(_pClient);
                 throw Networking::NetworkException(_pClient.clientSocket, 0, "Server send failed (payload, 0 bytes sent)");
            }

            currentBufferPosition += payloadBytesSent;
            remainingBytes -= payloadBytesSent;
            totalBytesSuccessfullySent += payloadBytesSent;
            if (remainingBytes > 0) {
                 Logger::getInstance().log(LogLevel::INFO, "Server::Send (payload): Partial send. " + std::to_string(payloadBytesSent) + " sent, " + std::to_string(remainingBytes) + " remaining for client " + GetClientIPAddress(_pClient) + ". Continuing send.");
                 // No need to increment send_attempts here unless we consider partial sends as needing a full retry cycle
            }
        }

        if (remainingBytes > 0) { // MAX_RETRIES exceeded or other loop exit before full send
            Logger::getInstance().log(LogLevel::ERROR, "Server::Send: Failed to send full payload after " + std::to_string(MAX_RETRIES) + " attempts or due to partial sends to client " + GetClientIPAddress(_pClient) + ". Sent " + std::to_string(length - remainingBytes) + "/" + std::to_string(length) + " bytes.");
            DisconnectClient(_pClient);
            throw Networking::NetworkException(_pClient.clientSocket, 0, "Server send failed (incomplete payload after retries)");
        }
    } else if (length == 0) {
        // For zero-length messages, header is sent, payload part is skipped.
        Logger::getInstance().log(LogLevel::DEBUG, "Server::Send: Zero-length message, only header sent to client " + GetClientIPAddress(_pClient));
    }


    return totalBytesSuccessfullySent; // total header + payload bytes
}

// Send data to a specified address and port (UDP, not modified for length prefix)
int Networking::Server::SendTo(const char* _pBuffer, int length, PCSTR _pAddress, int _pPort)
{
	static int retries=0;
	int bytesSent=0;
	// Create a sockaddr_in structure to hold the address and port of the recipient
	sockaddr_storage sockAddress;
	ZeroMemory(&sockAddress, sizeof(sockAddress));
	if (serverInfo.sin_family == AF_INET)
	{
		// Zero out the sockaddr_in structure

		sockaddr_in* recipient =(sockaddr_in*) &sockAddress;
		// Set the address family, port, and address of the recipient
		recipient->sin_family = serverInfo.sin_family;
		recipient->sin_port = htons(_pPort);
		inet_pton(serverInfo.sin_family, _pAddress, &recipient->sin_addr);
	}

	else if(serverInfo.sin_family == AF_INET6)
	{


		sockaddr_in6* recipient =(sockaddr_in6*) &sockAddress;
		// Set the address family, port, and address of the recipient
		recipient->sin6_family = serverInfo.sin_family;
		recipient->sin6_port = htons(_pPort);
		inet_pton(serverInfo.sin_family, _pAddress, &recipient->sin6_addr);
	}

	try{
		// Send the data to the specified recipient
		// Note: Using 'length' parameter now instead of strlen(_pBuffer)
		bytesSent = sendto(serverSocket, _pBuffer, length, 0, (sockaddr*)&sockAddress, sizeof(sockAddress));

		// If there was an error, throw an exception
		if(bytesSent == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();
			ThrowSendException(serverSocket, errorCode);
		}

		retries = 0;
	}

	catch(Networking::NetworkException &ex)
	{
		switch(ex.GetErrorCode())
		{
		case EAGAIN:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				SendTo(_pBuffer, _pAddress, _pPort);
			}
			else
			{
                Logger::getInstance().log(LogLevel::ERROR, "SendTo failed (EAGAIN) after max retries to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
				break;
			}

		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "SendTo failed (EINTR) to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ", retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				SendTo(_pBuffer, _pAddress, _pPort);
			}
			else
			{
                Logger::getInstance().log(LogLevel::ERROR, "SendTo failed (EINTR) after max retries to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
				break;
			}

		case EINPROGRESS:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "SendTo failed (EINPROGRESS) to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ", retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				SendTo(_pBuffer, _pAddress, _pPort);
			}
			else
			{
                Logger::getInstance().log(LogLevel::ERROR, "SendTo failed (EINPROGRESS) after max retries to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
				break;
			}

		default:
            Logger::getInstance().log(LogLevel::ERROR, "SendTo failed (default case) to " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
			break;

		}
	}
	// Return  the number of bytes sent if the data was sent successfully
	return bytesSent;
}


// Send data to all connected clients
int Networking::Server::SendToAll(const char* _pSendBuffer, int length)
{
    int totalBytesSentAcrossClients = 0;
    // Iterate over a copy of clients vector if DisconnectClient can modify the original 'clients' list
    // and cause iterator invalidation. Standard practice for such patterns.
    std::vector<Networking::ClientConnection> clients_copy = clients;

    for (auto& client : clients_copy) // Use reference for client
    {
        try
        {
            // Call the modified Send method which now handles its own retries and exceptions
            totalBytesSentAcrossClients += Send(_pSendBuffer, length, client);
        }
        catch(const Networking::NetworkException &ex)
        {
            // The Send method already logs errors and disconnects the client on failure.
            // Log here that this specific client failed in SendToAll context.
            Logger::getInstance().log(LogLevel::ERROR, "SendToAll: Failed to send to client " + GetClientIPAddress(client) + ". Error: " + ex.what());
            // No need to DisconnectClient(client) here as Send() should have handled it.
            // If Send() did not throw but returned error, that would be different.
            // But Send() is designed to throw on unrecoverable error.
        }
        catch(const std::exception &ex) // Catch other std::exceptions
        {
            Logger::getInstance().log(LogLevel::ERROR, "SendToAll: Std::exception while sending to client " + GetClientIPAddress(client) + ". Error: " + ex.what());
            // Consider if client needs disconnection here. If Send() was successful but something else failed.
            // Assuming Send() is the primary point of failure that would require disconnect.
        }
        // Catch all (...) could be added if necessary
    }

    // The return value is a bit ambiguous in the original.
    // Returning total bytes sent to all clients successfully, or 0 if all failed.
    // Or could return number of clients successfully sent to.
    // For now, sum of bytes.
    return totalBytesSentAcrossClients;
}



// Send a file to the client
void Networking::Server::SendFile(const std::string& _pFilePath, Networking::ClientConnection client)
{
	// Open the file for reading
	std::ifstream file(_pFilePath, std::ios::in | std::ios::binary);

	// Check if the file was opened successfully
	if(!file)
	{
		// Throw an exception if the file could not be opened
		throw std::runtime_error("Error: Unable to open file '" + _pFilePath + "'");
	}

	// Read the file data into a buffer
	std::vector<char> fileData((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));

	// Send the file data to the client
    if (!fileData.empty()) {
        Send(fileData.data(), static_cast<int>(fileData.size()), client);
    } else {
        // Handle empty file case: send a zero-length message
        Send(nullptr, 0, client);
    }
    Logger::getInstance().log(LogLevel::INFO, "Sent file " + _pFilePath + " to client " + GetClientIPAddress(client));
}


// Receive data from the server
std::vector <char> Networking::Server::Receive(Networking::ClientConnection client)
{
	static int retries =0;
	// Initialize the number of bytes received to 0
	int bytesReceived =0;

	// Create a vector to store the received data
	std::vector<char> receiveBuffer;
	try{
		// Receive data from the server in a loop
		do{
			// Get the current size of the receive buffer
			int bufferStart = receiveBuffer.size();
			// Resize the buffer to make room for more data
			receiveBuffer.resize(bufferStart+512);
			// Receive data from the server
			bytesReceived = recv(client.clientSocket, &receiveBuffer[bufferStart],512,0);
			// Resize the buffer to the actual size of the received data
			receiveBuffer.resize(bufferStart + bytesReceived);
		} while (bytesReceived == 512);

// If there was an error, throw an exception
		if (bytesReceived == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();
			Networking::ThrowReceiveException(client.clientSocket,errorCode);
		}
		retries =0;
	}
	catch(Networking::NetworkException &ex)
	{
		switch(ex.GetErrorCode())
		{
		case EAGAIN:
			if(retries < MAX_RETRIES)
			{
				retries++;
				Receive(client);
			}
			else{
				retries =0;
				DisconnectClient(client);
                Logger::getInstance().log(LogLevel::ERROR, "Receive failed (EAGAIN) after max retries from client " + GetClientIPAddress(client) + ": " + std::string(ex.what()));
				break;
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "Receive failed (EINTR) from client " + GetClientIPAddress(client) + ", retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				Receive(client);
			}
			else{
				retries =0;
				DisconnectClient(client);
                Logger::getInstance().log(LogLevel::ERROR, "Receive failed (EINTR) after max retries from client " + GetClientIPAddress(client) + ": " + std::string(ex.what()));
				break;
			}
		default:
			DisconnectClient(client);
            Logger::getInstance().log(LogLevel::ERROR, "Receive failed (default case) from client " + GetClientIPAddress(client) + ": " + std::string(ex.what()));
			break;
		}
	}

	// Return the vector containing the received data
	return receiveBuffer;
}


// Receive data from a specified address and port
std::vector<char> Networking::Server::ReceiveFrom(const char* _pAddress, int _pPort) // Changed PCSTR to const char*
{
	// Initialize the number of bytes received to 0
	int bytesReceived =0;
	static int retries =0;
	std::vector<char> receiveBuffer;
	try{
		sockaddr_storage sockAddress;
		ZeroMemory(&sockAddress, sizeof(sockAddress));
		if(serverInfo.sin_family == AF_INET)
		{

			// Create a sockaddr_in structure to hold the address and port of the sender
			sockaddr_in* sender = (sockaddr_in*) &sockAddress;

			// Set the address family, port, and address of the sender
			sender->sin_family = AF_INET;
			sender->sin_port = htons(_pPort);
			inet_pton(AF_INET, _pAddress, &sender->sin_addr);
		}

		else if (serverInfo.sin_family == AF_INET6)
		{
			// Create a sockaddr_in structure to hold the address and port of the sender
			sockaddr_in6* sender = (sockaddr_in6*) &sockAddress;

			// Set the address family, port, and address of the sender
			sender->sin6_family = AF_INET6;
			sender->sin6_port = htons(_pPort);
			inet_pton(AF_INET6, _pAddress, &sender->sin6_addr);
		}


		// Receive data from the server in a loop
		do{
			// Get the current size of the receive buffer
			int bufferStart = receiveBuffer.size();
			// Resize the buffer to make room for more data
			receiveBuffer.resize(bufferStart+512);
			// Receive data from the server
			bytesReceived = recvfrom(serverSocket, &receiveBuffer[bufferStart], 512, 0, (sockaddr*)&sockAddress, (socklen_t*)sizeof(sockAddress));
			// Resize the buffer to the actual size of the received data
			receiveBuffer.resize(bufferStart + bytesReceived);
		} while (bytesReceived == 512);


		// If there was an error, throw an exception
		if(bytesReceived == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();

			ThrowReceiveException(serverSocket,errorCode);
		}
		retries =0;
	}

	catch(Networking::NetworkException &ex)
	{
		switch(ex.GetErrorCode())
		{
		case EAGAIN:
			if(retries < MAX_RETRIES)
			{
				retries++;
				ReceiveFrom(_pAddress, _pPort);
			}
			else{
				retries =0;
                Logger::getInstance().log(LogLevel::ERROR, "ReceiveFrom failed (EAGAIN) after max retries from " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
				break;
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
                Logger::getInstance().log(LogLevel::WARN, "ReceiveFrom failed (EINTR) from " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ", retrying (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + "): " + std::string(ex.what()));
				ReceiveFrom(_pAddress, _pPort);
			}
			else{
				retries =0;
                Logger::getInstance().log(LogLevel::ERROR, "ReceiveFrom failed (EINTR) after max retries from " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
				break;
			}
		default:
            Logger::getInstance().log(LogLevel::ERROR, "ReceiveFrom failed (default case) from " + std::string(_pAddress) + ":" + std::to_string(_pPort) + ": " + std::string(ex.what()));
			break;
		}
	}
	// Return the received data
	return receiveBuffer;
}

// Receive a file from the server
void Networking::Server::ReceiveFile(const std::string& _pFilePath, Networking::ClientConnection client)
{
	// Open the file for writing
	std::ofstream file(_pFilePath, std::ios::out | std::ios::binary);

	// Check if the file was opened successfully
	if(!file)
	{
		// Throw an exception if the file could not be opened
		throw std::runtime_error("Error: Unable to open file '" + _pFilePath + "'");
	}

	// Receive the file data from the server
	std::vector<char> fileData = Receive(client);

	// Write the file data to the file
	file.write(&fileData[0], fileData.size());
    Logger::getInstance().log(LogLevel::INFO, "Received file " + _pFilePath + " from client " + GetClientIPAddress(client));
}


bool Networking::Server::ServerIsRunning()
{
	return serverIsConnected;
}


void Networking::Server::DisconnectClient(Networking::ClientConnection _pClient)

{
	try{
		// Disconnect the client from the server
		// Shut down the server socket
    #ifdef _WIN32
		int shutdownResult = shutdown(_pClient.clientSocket, SD_BOTH);
    #else
		int shutdownResult = shutdown(_pClient.clientSocket, SHUT_RDWR);
    #endif
		// If there was an error, throw an exception
		if (shutdownResult == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();

			// Throw the error code
			ThrowShutdownException(_pClient.clientSocket, errorCode);
		}
	}
	catch (Networking::NetworkException &ex)
	{
        Logger::getInstance().log(LogLevel::ERROR, "Error during DisconnectClient for " + GetClientIPAddress(_pClient) + ": " + std::string(ex.what()));
	}
    Logger::getInstance().log(LogLevel::INFO, "Disconnecting client " + GetClientIPAddress(_pClient) + " socket " + std::to_string(_pClient.clientSocket));
	// Close the socket
	CLOSESOCKET(_pClient.clientSocket);

	// Remove the client from the list of clients
	clients.erase(std::remove(clients.begin(), clients.end(), _pClient), clients.end());
}


// Shut down the server
void Networking::Server::Shutdown()
{
    Logger::getInstance().log(LogLevel::INFO, "Server shutdown initiated.");

    // First, disconnect all active clients
    Logger::getInstance().log(LogLevel::INFO, "Starting disconnection of all clients...");
    auto clients_copy = clients; // Create a copy to iterate safely
    for (auto& client_conn : clients_copy) {
        try {
            Logger::getInstance().log(LogLevel::DEBUG, "Attempting to disconnect client: " + GetClientIPAddress(client_conn) + " on socket " + std::to_string(client_conn.clientSocket));
            DisconnectClient(client_conn);
            Logger::getInstance().log(LogLevel::INFO, "Successfully disconnected client: " + GetClientIPAddress(client_conn));
        } catch (const Networking::NetworkException& e) {
            Logger::getInstance().log(LogLevel::ERROR, "Error disconnecting client " + GetClientIPAddress(client_conn) + " during server shutdown: " + std::string(e.what()));
        } catch (const std::exception& e) { // Catch any other standard exceptions
            Logger::getInstance().log(LogLevel::ERROR, "Standard exception disconnecting client " + GetClientIPAddress(client_conn) + " during server shutdown: " + std::string(e.what()));
        }
        catch (...) { // Catch-all for any other unknown exceptions
            Logger::getInstance().log(LogLevel::ERROR, "Unknown error disconnecting client " + GetClientIPAddress(client_conn) + " during server shutdown.");
        }
    }
    Logger::getInstance().log(LogLevel::INFO, "All clients processed for disconnection.");

	try{
		// Disconnect the server socket
		if (serverIsConnected)
		{
			// Shut down the server socket
	#ifdef _WIN32
			int shutdownResult= shutdown(serverSocket, SD_BOTH);
	#else
			int shutdownResult= shutdown(serverSocket, SHUT_RDWR);
	#endif


			// Check if the socket was successfully shut down
			if (shutdownResult == SOCKET_ERROR)
			{
				// Get the error code
				int errorCode = GETERROR();

				// Close the socket
				CLOSESOCKET(serverSocket);

				// Throw the error code
				ThrowShutdownException(serverSocket, errorCode);
			}

			// Close the server socket
			CLOSESOCKET(serverSocket);
            Logger::getInstance().log(LogLevel::INFO, "Main server socket shut down and closed.");
		}
	}

	catch (Networking::NetworkException &ex)
	{
        Logger::getInstance().log(LogLevel::ERROR, "Error during server socket shutdown: " + std::string(ex.what()));
	}
    // Logger::getInstance().log(LogLevel::INFO, "Server shutdown sequence complete."); // Moved this log to be more accurate

	#ifdef _WIN32
	// Clean up the Windows Sockets DLL
	WSACleanup();
	#endif

	// Set the server connected flag to false
	serverIsConnected = false;
    Logger::getInstance().log(LogLevel::INFO, "Server fully shut down and cleaned up.");
}


// Return a vector of ClientConnection objects representing the currently connected clients
std::vector<Networking::ClientConnection> Networking::Server::getClients() const
{
	return clients;
}

std::string Networking::Server::GetClientIPAddress(Networking::ClientConnection _pClient)
{
	std::string ip;
	if(GetServerType() == Networking::ServerType::IPv4)
		ip = inet_ntoa(_pClient.clientInfo.sin_addr);
	else {
		ip.resize(INET6_ADDRSTRLEN);
		inet_ntop(AF_INET6, &_pClient.clientInfo6.sin6_addr, &ip[0],INET6_ADDRSTRLEN);
	}
	return ip;
}

Networking::ServerType Networking::Server::GetServerType()
{
	return serverType_;
}

// Removed LogToFile and LogToConsole methods as they are replaced by direct Logger::getInstance() calls

int Networking::Server::GetPort()
{
	return ntohs(serverInfo.sin_port);
}