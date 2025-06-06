#include "utilities/server.h"
#include "utilities/logger.h" // Ensure logger is included for Logger::getInstance()
#include <string>   // For std::string conversion if needed (e.g. ex.what())
#include <iostream> // For std::cout (verbose logging)
#include <iomanip>  // For std::put_time (used by getNetworkTimestamp)
#include <sstream>  // For std::ostringstream (used by getNetworkTimestamp)
#include <thread>   // For std::this_thread::get_id()
#include <chrono>   // For timestamp components (used by getNetworkTimestamp)
#include <unistd.h> // For geteuid

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
        Logger::getInstance().log(LogLevel::ERROR, "startListening failed: " + std::string(ne.what())); // Changed FATAL to ERROR
        serverIsConnected = false; // Ensure this is false on failure
        return false;
    } catch (const std::exception& e) {
        std::cout << "[VERBOSE LOG Server " << this << " " << getNetworkTimestamp() << " TID: " << std::this_thread::get_id() << "] startListening: std::exception: " << e.what() << ". Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::ERROR, "startListening failed with std::exception: " + std::string(e.what())); // Changed FATAL to ERROR
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


    ZeroMemory(&addressInfo, sizeof(addressInfo));
    SetFamily(addressFamily);
    SetSocketType(SOCK_STREAM);
    SetProtocol(IPPROTO_TCP);

    // Set up the sockaddr structures for IPv4 or IPv6
    memset(&serverInfo, 0, sizeof(serverInfo));
    memset(&serverInfo6, 0, sizeof(serverInfo6));

    if (addressFamily == AF_INET6) {
        serverInfo6.sin6_family = AF_INET6;
        serverInfo6.sin6_addr = in6addr_any;
        serverInfo6.sin6_port = htons(portNumber_);
        serverInfo6.sin6_flowinfo = 0;
        serverInfo6.sin6_scope_id = 0;
        serverInfo.sin_family = AF_INET6; // for functions still using serverInfo
    } else {
        serverInfo.sin_family = AF_INET;
        serverInfo.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on all interfaces
        serverInfo.sin_port = htons(portNumber_); // Use member variable
    }

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

        // SO_REUSEPORT option removed to ensure ServerInitializationOnSamePort test fails the second bind.
        // // Set SO_REUSEPORT option (more platform specific)
        // // SO_REUSEPORT allows multiple sockets to bind to the same IP address and port.
        // // This is particularly useful on Linux for load distribution or fast restarts.
        // // On Windows, SO_REUSEADDR effectively allows similar behavior for TCP sockets
        // // regarding quick restarts, but SO_REUSEPORT is not typically used or available in the same way.
        // #if defined(SO_REUSEPORT) && !defined(_WIN32)
        // int reuseport = 1;
        // if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseport, sizeof(reuseport)) < 0) {
        //     int errorCode = GETERROR();
        //     Logger::getInstance().log(LogLevel::WARN, "setsockopt(SO_REUSEPORT) failed with error code " + std::to_string(errorCode) + ": " + std::string(strerror(errorCode)));
        //     // Log and continue
        // } else {
        //     Logger::getInstance().log(LogLevel::DEBUG, "Successfully set SO_REUSEPORT on server socket.");
        // }
        // #endif

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
    if (portNumber_ < 1024 && ::geteuid() != 0) {
        Logger::getInstance().log(LogLevel::ERROR,
            "Cannot bind to privileged port " + std::to_string(portNumber_) +
            " as non-root user");
        ThrowBindException(serverSocket, EACCES);
    }
    // Bind the socket to a local address and port
    try{
                const sockaddr* addr = nullptr;
                socklen_t addrlen = 0;
                if(serverInfo.sin_family == AF_INET6) {
                    addr = (sockaddr*)&serverInfo6;
                    addrlen = sizeof(serverInfo6);
                } else {
                    addr = (sockaddr*)&serverInfo;
                    addrlen = sizeof(serverInfo);
                }

                if (bind(serverSocket, addr, addrlen) == SOCKET_ERROR)
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
				Logger::getInstance().log(LogLevel::ERROR, "BindSocket failed (EADDRINUSE) after max retries: " + std::string(ex.what()));
                throw; // Re-throw the exception
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
				Logger::getInstance().log(LogLevel::ERROR, "BindSocket failed (EADDRNOTAVAIL) after max retries: " + std::string(ex.what()));
                throw; // Re-throw the exception
			}

		default:
			Logger::getInstance().log(LogLevel::ERROR, "BindSocket failed (default case): " + std::string(ex.what()));
            throw; // Re-throw the exception
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
			throw ex;

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
			throw ex;

			#endif

		default:
            Logger::getInstance().log(LogLevel::ERROR, "Accept failed (default case): " + std::string(ex.what()));
			throw ex;
		}
	}

// Add the client connection to the list of clients
    { // Scope for lock_guard
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients.push_back(client);
    }
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


// Helper function to send all data in a buffer (Server context)
// Note: _pClient is passed by value, but its clientSocket is used.
// A reference or pointer to a ClientConnection might be more appropriate if its state needed modification by send_all.
// For now, this helper primarily encapsulates the send loop and basic error handling.
// It does not modify ClientConnection state directly but throws on error, which the caller (Server::Send) handles.
static bool send_all_server(SOCKET sock, const char* buffer, size_t length, Networking::Server* server_this, Networking::ClientConnection& client_conn_ref) {
    size_t totalBytesSent = 0;
    while (totalBytesSent < length) {
        int bytesSent = send(sock, buffer + totalBytesSent, length - totalBytesSent, 0);
        if (bytesSent == SOCKET_ERROR) {
            int errorCode = GETERROR();
            Logger::getInstance().log(LogLevel::ERROR, "send_all_server: send() failed for socket " + std::to_string(sock) + " with error: " + std::to_string(errorCode));
            // Server-side, we might disconnect this specific client rather than shutting down the whole server socket.
            // The original Server::Send logic calls DisconnectClient.
            // Throwing here allows Server::Send to catch and decide.
            throw Networking::NetworkException(sock, errorCode, "send_all_server failed");
        }
        if (bytesSent == 0) { // Should not happen with blocking sockets unless length is 0
            Logger::getInstance().log(LogLevel::ERROR, "send_all_server: send() returned 0 for socket " + std::to_string(sock) + ", treating as error.");
            throw Networking::NetworkException(sock, 0, "send_all_server failed: sent 0 bytes");
        }
        totalBytesSent += bytesSent;
    }
    return true;
}


// Send data to the client (modified for length-prefixing)
int Networking::Server::Send(PCSTR _pSendBuffer, Networking::ClientConnection _pClient)
{
    // Note: _pClient is a copy. Operations on its socket are fine, but state changes to _pClient itself won't persist outside.
    // The current DisconnectClient takes a ClientConnection copy too. This design might need review for state management.

    size_t payloadLength = strlen(_pSendBuffer);
    uint32_t netPayloadLength = htonl(static_cast<uint32_t>(payloadLength));

    Logger::getInstance().log(LogLevel::DEBUG, "[Server::Send to " + GetClientIPAddress(_pClient) + "] Sending header: payloadLength = " + std::to_string(payloadLength));

    try {
        // Send the header (payload length)
        send_all_server(_pClient.clientSocket, reinterpret_cast<const char*>(&netPayloadLength), sizeof(netPayloadLength), this, _pClient);

        Logger::getInstance().log(LogLevel::DEBUG, "[Server::Send to " + GetClientIPAddress(_pClient) + "] Header sent. Sending payload...");

        // Send the actual payload
        if (payloadLength > 0) {
            send_all_server(_pClient.clientSocket, _pSendBuffer, payloadLength, this, _pClient);
        }
        Logger::getInstance().log(LogLevel::DEBUG, "[Server::Send to " + GetClientIPAddress(_pClient) + "] Payload sent successfully.");
        return static_cast<int>(payloadLength); // Return payload length
    } catch (const Networking::NetworkException &ex) {
        // send_all_server throws, but doesn't DisconnectClient. Server::Send's original catch block did.
        Logger::getInstance().log(LogLevel::ERROR, "Server::Send failed for client " + GetClientIPAddress(_pClient) + " (socket " + std::to_string(_pClient.clientSocket) + "): " + std::string(ex.what()));
        DisconnectClient(_pClient); // Disconnect on failure as per original Send logic
        // Decide whether to re-throw or return an error code. Original threw via ThrowSendException.
        // For now, let's stick to re-throwing or throwing a new one, as just returning -1 might hide errors.
        // The original Send method's catch block was more complex with retries.
        // This simplified version for length-prefixing assumes send_all handles immediate errors by throwing.
        // If retries are needed, they'd be inside send_all_server or here.
        // For now, let's throw a generic exception indicating send failure to this client.
        Networking::ThrowSendException(_pClient.clientSocket, ex.GetErrorCode()); // Re-throw specific error
        return -1; // Should not be reached if ThrowSendException exits or throws
    }
}

// Send data to a specified address and port
int Networking::Server::SendTo(PCSTR _pBuffer, PCSTR _pAddress, int _pPort)
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
		bytesSent = sendto(serverSocket, _pBuffer, strlen(_pBuffer), 0, (sockaddr*)&sockAddress, sizeof(sockAddress));

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
int Networking::Server::SendToAll(PCSTR _pSendBuffer)
{

	int bytesSent;
	int retries =0;
	// Iterate over all connected clients
    std::vector<Networking::ClientConnection> clients_copy_for_send;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_copy_for_send = clients;
    }
	for (auto client : clients_copy_for_send) // Iterate over the copy
	{
		try
		{
			// Send the data to the current client
			bytesSent = send(client.clientSocket, _pSendBuffer, strlen(_pSendBuffer), 0);

			// If there was an error, throw an exception
			if (bytesSent == SOCKET_ERROR)
			{
				// Get the error code
				int errorCode = GETERROR();
				ThrowSendException(client.clientSocket, errorCode);
			}
		}

		catch(Networking::NetworkException &ex)
		{
			switch(ex.GetErrorCode())
			{
			case EAGAIN:
				retries =0;
				while(retries < MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					retries++;
					std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
					bytesSent = send(client.clientSocket, _pSendBuffer, strlen(_pSendBuffer), 0);
				}

				if(retries == MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					DisconnectClient(client);
				}

				break;

			case EINPROGRESS:

				retries =0;
				while(retries < MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					retries++;
					std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
					bytesSent = send(client.clientSocket, _pSendBuffer, strlen(_pSendBuffer), 0);
				}

				if(retries == MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					DisconnectClient(client);
				}

				break;


			case EINTR:
				retries =0;
				while(retries < MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					retries++;
					std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
					bytesSent = send(client.clientSocket, _pSendBuffer, strlen(_pSendBuffer), 0);
				}

				if(retries == MAX_RETRIES && bytesSent == SOCKET_ERROR)
				{
					DisconnectClient(client);
				}

				break;


			default:
				DisconnectClient(client);
				break;
			}

		}
	}

	// Return the number of bytes sent if the data was sent successfully
	return bytesSent;
}



// Send a file to the server
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

	// Send the file data to the server
	Send(&fileData[0], client); // Assuming Send already logs its own errors/retries
    Logger::getInstance().log(LogLevel::INFO, "Sent file " + _pFilePath + " to client " + GetClientIPAddress(client));
}


// Helper function to receive exactly 'length' bytes (Server context)
static bool recv_all_server(SOCKET sock, char* buffer, size_t length, Networking::Server* server_this, Networking::ClientConnection& client_conn_ref) {
    size_t totalBytesReceived = 0;
    while (totalBytesReceived < length) {
        int bytesReceived = recv(sock, buffer + totalBytesReceived, length - totalBytesReceived, 0);
        if (bytesReceived == 0) { // Graceful shutdown by peer
            Logger::getInstance().log(LogLevel::INFO, "recv_all_server: Peer (socket " + std::to_string(sock) + ") has performed an orderly shutdown during message reception.");
            // Server-side, this means the client closed the connection.
            // We don't necessarily close the server's main listening socket here.
            // We should signal that this specific client connection is effectively dead.
            // Throwing an exception allows Receive to handle cleanup for this client.
            throw Networking::NetworkException(sock, 0, "recv_all_server failed: Peer shutdown prematurely");
        }
        if (bytesReceived == SOCKET_ERROR) {
            int errorCode = GETERROR();
            Logger::getInstance().log(LogLevel::ERROR, "recv_all_server: recv() failed for socket " + std::to_string(sock) + " with error: " + std::to_string(errorCode));
            throw Networking::NetworkException(sock, errorCode, "recv_all_server failed");
        }
        totalBytesReceived += bytesReceived;
    }
    return true;
}

// Receive data from the client (modified for length-prefixing)
std::vector<char> Networking::Server::Receive(Networking::ClientConnection client) {
    // Note: 'client' is a copy. Operations on its socket are fine.
    // If recv_all_server or this function needs to modify the state of the client connection
    // as stored in Server's 'clients' list, a reference or identifier would be needed.

    uint32_t netPayloadLength;
    Logger::getInstance().log(LogLevel::DEBUG, "[Server::Receive from " + GetClientIPAddress(client) + "] Receiving header (4 bytes).");
    try {
        recv_all_server(client.clientSocket, reinterpret_cast<char*>(&netPayloadLength), sizeof(netPayloadLength), this, client);
    } catch (const Networking::NetworkException& e) {
        if (e.GetErrorCode() == 0) { // Peer shutdown during header read
            Logger::getInstance().log(LogLevel::INFO, "Server::Receive: Client " + GetClientIPAddress(client) + " shutdown while reading header.");
            DisconnectClient(client); // Clean up this client connection
            return {}; // Return empty vector, indicating no message received due to client disconnect
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "Server::Receive: Failed to receive header from client " + GetClientIPAddress(client) + ": " + std::string(e.what()));
            DisconnectClient(client);
            Networking::ThrowReceiveException(client.clientSocket, e.GetErrorCode()); // Re-throw as original Receive would
            return {}; // Should not be reached
        }
    }

    uint32_t payloadLength = ntohl(netPayloadLength);
    Logger::getInstance().log(LogLevel::DEBUG, "[Server::Receive from " + GetClientIPAddress(client) + "] Header received. Payload length = " + std::to_string(payloadLength));

    if (payloadLength == 0) {
        Logger::getInstance().log(LogLevel::DEBUG, "[Server::Receive from " + GetClientIPAddress(client) + "] Zero-length payload indicated. Returning empty vector.");
        return {}; // Correctly handle zero-length message
    }

    std::vector<char> payloadBuffer(payloadLength);
    Logger::getInstance().log(LogLevel::DEBUG, "[Server::Receive from " + GetClientIPAddress(client) + "] Receiving payload (" + std::to_string(payloadLength) + " bytes).");
    try {
        recv_all_server(client.clientSocket, payloadBuffer.data(), payloadLength, this, client);
    } catch (const Networking::NetworkException& e) {
        if (e.GetErrorCode() == 0) { // Peer shutdown during payload read
            Logger::getInstance().log(LogLevel::INFO, "Server::Receive: Client " + GetClientIPAddress(client) + " shutdown while reading payload.");
        } else {
            Logger::getInstance().log(LogLevel::ERROR, "Server::Receive: Failed to receive payload from client " + GetClientIPAddress(client) + ": " + std::string(e.what()));
        }
        DisconnectClient(client); // Clean up on any error/EOF during payload read
        // Decide if to return partial data, throw, or return empty.
        // For robustness, if we didn't get the full expected payload, it's an error or incomplete message.
        // Original Receive would throw or retry. Let's throw an error indicating incomplete message.
        Networking::ThrowReceiveException(client.clientSocket, e.GetErrorCode() != 0 ? e.GetErrorCode() : -1 /* generic error for EOF mid-message */);
        return {}; // Should not be reached
    }

    Logger::getInstance().log(LogLevel::DEBUG, "[Server::Receive from " + GetClientIPAddress(client) + "] Payload received successfully.");
    return payloadBuffer;
}


// Receive data from a specified address and port
std::vector<char> Networking::Server::ReceiveFrom(PCSTR _pAddress, int _pPort)
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
    std::lock_guard<std::mutex> lock(clients_mutex_);
	clients.erase(std::remove(clients.begin(), clients.end(), _pClient), clients.end());
}


// Shut down the server
void Networking::Server::Shutdown()
{
    Logger::getInstance().log(LogLevel::INFO, "Server shutdown initiated.");

    // First, disconnect all active clients
    Logger::getInstance().log(LogLevel::INFO, "Starting disconnection of all clients...");
    std::vector<Networking::ClientConnection> clients_copy_for_shutdown;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_copy_for_shutdown = clients; // Create a copy to iterate safely
    }
    for (auto& client_conn : clients_copy_for_shutdown) { // Iterate over the copy
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
    std::lock_guard<std::mutex> lock(clients_mutex_);
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
        if(serverInfo.sin_family == AF_INET6) {
            return ntohs(serverInfo6.sin6_port);
        }
        return ntohs(serverInfo.sin_port);
}