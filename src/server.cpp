#include "server.h"

Networking::Server::Server(int _pPortNumber, ServerType _pServerType,const std::string& _pLogFile) : logger(_pLogFile)
{
	serverType = _pServerType;
	Networking::Server::InitServer();
	Networking::Server::CreateServerSocket(_pPortNumber, _pServerType);

}


Networking::Server::~Server()
{
}

bool Networking::Server::InitServer()
{
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
		logger.log(<ex.what());
		std::exit(EXIT_FAILURE);
	}

   #endif

	return true;
}




bool Networking::Server::CreateServerSocket(int _pPortNumber,  ServerType _pServerType)
{
	// Set the address family based on the server type
	int addressFamily = _pServerType == ServerType::IPv4 ? AF_INET : AF_INET6;
	ZeroMemory(&addressInfo, sizeof(addressInfo));
	SetFamily(addressFamily);
	SetSocketType(SOCK_STREAM);
	SetProtocol(IPPROTO_TCP);



// Set up the sockaddr_in structure
	serverInfo.sin_family = addressInfo.ai_family;
	serverInfo.sin_addr.s_addr = inet_addr("127.0.0.1");
	serverInfo.sin_port = htons(_pPortNumber);

	CreateSocket();
// Bind the server socket to a port
	BindSocket();

// Start listening for incoming connections
	ListenOnSocket();

	serverIsConnected = true;
// The server socket was created successfully
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
		retries =0;
	}

	catch(Networking::NetworkException &ex)
	{
		switch (ex.GetErrorCode())
		{
		case EACCES:
			logger.log(ex.what());
			std::exit(EXIT_FAILURE);

		case EAFNOSUPPORT:
			logger.log(ex.what());
			std::exit(EXIT_FAILURE);

		case EADDRINUSE:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				CreateSocket();
			}

			else
			{
				logger.log(ex.what());
				std::exit(EXIT_FAILURE);
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				CreateSocket();
			}

			else
			{
				logger.log(ex.what());
				std::exit(EXIT_FAILURE);
			}
		default:
			logger.log(ex.what());
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
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				BindSocket();
			}
			else
			{
				logger.log(ex.what());
				std::exit(EXIT_FAILURE);
			}
		case EADDRNOTAVAIL:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				BindSocket();
			}
			else
			{
				logger.log(ex.what());
				std::exit(EXIT_FAILURE);
			}

		default:
			logger.log(ex.what());
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
				logger.log(ex.what());
				std::exit(EXIT_FAILURE);
			}
		default:
			logger.log(ex.what());
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
			logger.log(ex.what());
			return Networking::ClientConnection();

			#else

		case EINTR:
			if(retries <  MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Accept();
			}

			logger.log(ex.what());
			return Networking::ClientConnection();

			#endif

		default:
			logger.log(ex.what());
			return Networking::ClientConnection();
		}
	}

// Add the client connection to the list of clients
	clients.push_back(client);
	logger.log("New connection from " + GetClientIPAddress(client));
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
int Networking::Server::Send(PCSTR _pSendBuffer, Networking::ClientConnection _pClient)
{
	static int retries =0;
	int bytesSent;
	try{

		// Send the data to the client
		bytesSent = send(_pClient.clientSocket,_pSendBuffer, strlen(_pSendBuffer),0 );

		// If there was an error, throw an exception
		if(bytesSent == SOCKET_ERROR)
		{
			// Get the error code
			int errorCode = GETERROR();

			// Throw the error code
			Networking::ThrowSendException(_pClient.clientSocket, errorCode);

		}
		retries =0;
	}

	catch (Networking::NetworkException &ex)
	{
		switch(ex.GetErrorCode())
		{
		case EAGAIN:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Send(_pSendBuffer, _pClient);
			}
			else
			{
				logger.log(ex.what());
				DisconnectClient(_pClient);
				break;
			}

		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Send(_pSendBuffer, _pClient);
			}
			else
			{
				logger.log(ex.what());
				DisconnectClient(_pClient);
				break;
			}

		case EINPROGRESS:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				Send(_pSendBuffer, _pClient);
			}
			else
			{
				logger.log(ex.what());
				DisconnectClient(_pClient);
				break;
			}

		default:
			logger.log(ex.what());
			DisconnectClient(_pClient);
			break;

		}

	}

	// Return  the number of bytes sent if the data was sent successfully
	return bytesSent;
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
				logger.log(ex.what());
				break;
			}

		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				SendTo(_pBuffer, _pAddress, _pPort);
			}
			else
			{
				logger.log(ex.what());
				break;
			}

		case EINPROGRESS:
			if(retries < MAX_RETRIES)
			{
				retries++;
				std::this_thread::sleep_for(std::chrono::seconds(RETRY_DELAY));
				SendTo(_pBuffer, _pAddress, _pPort);
			}
			else
			{
				logger.log(ex.what());
				break;
			}

		default:
			logger.log(ex.what());
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
	for (auto client : clients)
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
	Send(&fileData[0], client);
	logger.log("Sent " + _pFilePath + " to " + GetClientIPAddress(client));
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
				logger.log(ex.what());
				break;
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
				Receive(client);
			}
			else{
				retries =0;
				DisconnectClient(client);
				logger.log(ex.what());
				break;
			}
		default:
			DisconnectClient(client);
			logger.log(ex.what());
			break;
		}
	}

	// Return the vector containing the received data
	return receiveBuffer;
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
				logger.log(ex.what());
				break;
			}
		case EINTR:
			if(retries < MAX_RETRIES)
			{
				retries++;
				ReceiveFrom(_pAddress, _pPort);
			}
			else{
				retries =0;
				logger.log(ex.what());
				break;
			}
		default:
			logger.log(ex.what());
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
	logger.log("Received " + _pFilePath + " from " + GetClientIPAddress(client));
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

		logger.log(ex.what());
	}
	// Close the socket
	CLOSESOCKET(_pClient.clientSocket);

	// Remove the client from the list of clients
	clients.erase(std::remove(clients.begin(), clients.end(), _pClient), clients.end());
}


// Shut down the server
void Networking::Server::Shutdown()
{
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
		}
	}

	catch (Networking::NetworkException &ex)
	{
		logger.log(ex.what());
	}

	#ifdef _WIN32
	// Clean up the Windows Sockets DLL
	WSACleanup();
	#endif

	// Set the server connected flag to false
	serverIsConnected = false;
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
	return serverType;
}

void Networking::Server::LogToFile(const std::string& _pMessage)
{
	logger.log(_pMessage);
}

void Networking::Server::LogToConsole(const std::string& _pMessage)
{
	logger.logToConsole(_pMessage);
}

int Networking::Server::GetPort()
{
	return serverInfo.sin_port;
}