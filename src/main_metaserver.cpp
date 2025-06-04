// Main entry point for the Metaserver executable

#include <iostream> // For std::cerr, std::to_string
#include <thread>   // For std::thread
#include "utilities/server.h" // For Networking::Server, Networking::ClientConnection
#include "utilities/networkexception.h" // For Networking::NetworkException
#include "utilities/logger.h" // For Logger
#include "metaserver/metaserver.h" // For MetadataManager (declaration)

#include <cstdlib> // For std::atoi
#include <signal.h> // For signal(), SIGPIPE, SIG_IGN

// Declare global instances that will be defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// This allows main() to use them.
// extern Networking::Server server; // REMOVED
extern MetadataManager metadataManager;

// Declare HandleClientConnection which is defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// Alternatively, this declaration could be in a header file (e.g., metaserver.h if it's a free function related to the metaserver operations)
void HandleClientConnection(Networking::Server& server_instance, Networking::ClientConnection _pClient);

int main(int argc, char* argv[])
{
    // Ignore SIGPIPE: prevents termination if writing to a closed socket
    signal(SIGPIPE, SIG_IGN);

    int port = 50505; // Default port
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port == 0) { // Basic error check for atoi
            std::cerr << "FATAL: Invalid port number provided: " << argv[1] << std::endl;
            return 1;
        }
    }

    try {
        Logger::init(Logger::CONSOLE_ONLY_OUTPUT, LogLevel::DEBUG); // Attempt to use console output constant
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
        std::cerr << "DIAGNOSTIC: startListening() returned false. Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (startListening returned false). Port: " + std::to_string(local_server.GetPort()));
        return 1; // Exit if server cannot start
    }
    std::cerr << "DIAGNOSTIC: Returned from startListening() call. ServerIsRunning: " << (local_server.ServerIsRunning() ? "true" : "false") << std::endl;

    // local_server.ServerIsRunning() should now be true if startListening succeeded
    if (local_server.ServerIsRunning())
    {
        std::cerr << "DIAGNOSTIC: Entering while(true) accept loop." << std::endl;
        Logger::getInstance().log(LogLevel::INFO, "Metaserver is running and listening on port " + std::to_string(local_server.GetPort()));
        while (true)
        {
            std::cerr << "DIAGNOSTIC: Top of while(true) accept loop." << std::endl;
            try {
                Networking::ClientConnection client = local_server.Accept();
                // Assuming client.clientSocket is accessible and is the raw socket descriptor.
                // On POSIX, an invalid socket descriptor is often < 0.
                // This is a placeholder check; a proper check might involve client.isValid() or similar.
                if (client.clientSocket < 0) { // Placeholder for INVALIDSOCKET(client.clientSocket)
                     std::cerr << "DIAGNOSTIC: Main loop: local_server.Accept() returned an invalid client socket value: " << client.clientSocket << ". Will attempt to create thread anyway to observe behavior." << std::endl;
                }
                Logger::getInstance().log(LogLevel::INFO, "Accepted new client connection from " + local_server.GetClientIPAddress(client));
                std::cerr << "DIAGNOSTIC: Client accepted, about to create and detach thread." << std::endl;
                std::thread clientThread(HandleClientConnection, std::ref(local_server), client);
                clientThread.detach();
                Logger::getInstance().log(LogLevel::DEBUG, "Detached thread to handle client " + local_server.GetClientIPAddress(client));
            } catch (const Networking::NetworkException& ne) {
                Logger::getInstance().log(LogLevel::ERROR, "Network exception in main server loop: " + std::string(ne.what()));
                std::cerr << "DIAGNOSTIC: NetworkException in main loop: " << ne.what() << std::endl; // Also print to cerr
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in main server loop: " + std::string(e.what()));
                std::cerr << "DIAGNOSTIC: std::exception in main loop: " << e.what() << std::endl; // Also print to cerr
                break; // Exit on fatal error
            } catch (...) {
                Logger::getInstance().log(LogLevel::FATAL, "Unknown unhandled exception in main server loop.");
                std::cerr << "DIAGNOSTIC: Unknown exception in main loop." << std::endl; // Also print to cerr
                break; // Exit on fatal error
            }
        }
    } else {
        std::cerr << "DIAGNOSTIC: ServerIsRunning() is false after startListening() call. Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (ServerIsRunning is false).");
    }
    Logger::getInstance().log(LogLevel::INFO, "Metaserver shutting down.");
    return 0;
}
