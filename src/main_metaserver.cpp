// Main entry point for the Metaserver executable

#include <iostream> // For std::cerr, std::to_string
#include <thread>   // For std::thread
#include "utilities/server.h" // For Networking::Server, Networking::ClientConnection
#include "utilities/networkexception.h" // For Networking::NetworkException
#include "utilities/logger.h" // For Logger
#include "metaserver/metaserver.h" // For MetadataManager (declaration)

#include <cstdlib> // For std::atoi

// Declare global instances that will be defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// This allows main() to use them.
// extern Networking::Server server; // REMOVED
extern MetadataManager metadataManager;

// Declare HandleClientConnection which is defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// Alternatively, this declaration could be in a header file (e.g., metaserver.h if it's a free function related to the metaserver operations)
void HandleClientConnection(Networking::Server& server_instance, Networking::ClientConnection _pClient);

int main(int argc, char* argv[])
{
    int port = 50505; // Default port
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port == 0) { // Basic error check for atoi
            std::cerr << "FATAL: Invalid port number provided: " << argv[1] << std::endl;
            return 1;
        }
    }

    try {
        Logger::init("metaserver.log", LogLevel::INFO);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Logger initialization failed for metaserver: " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Metaserver starting up...");
    // Assuming loadMetadata is a public method of MetadataManager
    // and metadataManager instance is accessible (declared extern above).
    Logger::getInstance().log(LogLevel::INFO, "Loading metadata from file_metadata.dat and node_registry.dat");
    metadataManager.loadMetadata("file_metadata.dat", "node_registry.dat");

    Networking::Server local_server(port); // Create server instance with parsed port

    // Attempt to start the server
    if (!local_server.startListening()) {
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (startListening returned false). Port: " + std::to_string(local_server.GetPort()));
        return 1; // Exit if server cannot start
    }

    // local_server.ServerIsRunning() should now be true if startListening succeeded
    if (local_server.ServerIsRunning())
    {
        Logger::getInstance().log(LogLevel::INFO, "Metaserver is running and listening on port " + std::to_string(local_server.GetPort()));
        while (true)
        {
            try {
                Networking::ClientConnection client = local_server.Accept();
                Logger::getInstance().log(LogLevel::INFO, "Accepted new client connection from " + local_server.GetClientIPAddress(client));
                std::thread clientThread(HandleClientConnection, std::ref(local_server), client);
                clientThread.detach();
                Logger::getInstance().log(LogLevel::DEBUG, "Detached thread to handle client " + local_server.GetClientIPAddress(client));
            } catch (const Networking::NetworkException& ne) {
                Logger::getInstance().log(LogLevel::ERROR, "Network exception in main server loop: " + std::string(ne.what()));
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in main server loop: " + std::string(e.what()));
                break; // Exit on fatal error
            } catch (...) {
                Logger::getInstance().log(LogLevel::FATAL, "Unknown unhandled exception in main server loop.");
                break; // Exit on fatal error
            }
        }
    } else {
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (ServerIsRunning is false).");
    }
    Logger::getInstance().log(LogLevel::INFO, "Metaserver shutting down.");
    return 0;
}
