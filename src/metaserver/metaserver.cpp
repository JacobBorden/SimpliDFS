// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "utilities/filesystem.h"
#include "utilities/message.h" // Including message for node communication
#include <sstream>
#include "metaserver/metaserver.h"
#include "utilities/server.h"
#include "utilities/networkexception.h"
#include <thread>
#include <string> // Required for std::string constructor from vector iterators
#include "utilities/logger.h" // Include the Logger header

// Define persistence file paths and separators
// These were already added to metaserver.h in the previous step as global constants.
// If they were not, they would be defined here.
// const std::string FILE_METADATA_PATH = "file_metadata.dat";
// const std::string NODE_REGISTRY_PATH = "node_registry.dat";
// extern const char METADATA_SEPARATOR; // Defined in metaserver.h
// extern const char NODE_LIST_SEPARATOR; // Defined in metaserver.h


Networking::Server server(50505);
MetadataManager metadataManager;

void HandleClientConnection(Networking::ClientConnection _pClient)
{
    try {
        Logger::getInstance().log(LogLevel::DEBUG, "Handling client connection from " + server.GetClientIPAddress(_pClient));
        std::vector<char> received_vector = server.Receive(_pClient);
        if (received_vector.empty()) {
            // Handle empty receive, maybe client disconnected or sent no data
            Logger::getInstance().log(LogLevel::WARN, "Received empty data from client " + server.GetClientIPAddress(_pClient));
            // Depending on server logic, might want to close connection or return
            return; 
        }
        std::string received_data_str(received_vector.begin(), received_vector.end());
        Logger::getInstance().log(LogLevel::DEBUG, "Received data from " + server.GetClientIPAddress(_pClient) + ": " + received_data_str);
        Message request = Message::Deserialize(received_data_str);
        bool shouldSave = false;
        switch (request._Type)
    {
    case MessageType::CreateFile:
    {
        std::vector<std::string> nodes; // preferred nodes could be part of message
        metadataManager.addFile(request._Filename, nodes);
        shouldSave = true;
        break;
    }

    case MessageType::ReadFile:
    {
        std::vector<std::string> nodes = metadataManager.getFileNodes(request._Filename);
        break;
    }

    case MessageType::WriteFile:
    {
        std::vector<std::string> nodes = metadataManager.getFileNodes(request._Filename);
        break;
    }
    case MessageType::RegisterNode:
    {
        // Assuming _Filename carries nodeIdentifier, _NodeAddress carries IP, and _NodePort carries port
        metadataManager.registerNode(request._Filename, request._NodeAddress, request._NodePort);
        shouldSave = true;
        // Send a confirmation response back to the node
        server.Send("Node registered successfully", _pClient); // Actual send call
        Logger::getInstance().log(LogLevel::INFO, "Sent registration confirmation to node " + request._Filename);
        break;
    }
    case MessageType::Heartbeat:
    {
        Logger::getInstance().log(LogLevel::DEBUG, "Received Heartbeat from node " + request._Filename);
        metadataManager.processHeartbeat(request._Filename); // _Filename contains nodeIdentifier
        // For heartbeats, saving metadata might be too frequent.
        // Node liveness changes are saved by checkForDeadNodes if it's called and modifies state.
        // shouldSave = false; // Or true if every heartbeat should force a save of NodeInfo
        break;
    }
    case MessageType::DeleteFile: {
        Logger::getInstance().log(LogLevel::INFO, "[METASERVER] Received DeleteFile request for " + request._Filename);
        metadataManager.removeFile(request._Filename); // This will trigger notifications
        server.Send("Delete command processed.", _pClient);
        Logger::getInstance().log(LogLevel::INFO, "[METASERVER] Sent DeleteFile command processed confirmation for " + request._Filename);
        shouldSave = true; // Ensure metadata is saved
        break;
    }
    // Add cases for other metadata-modifying operations like RemoveFile if they exist
    default:
        Logger::getInstance().log(LogLevel::WARN, "Received unhandled message type: " + std::to_string(static_cast<int>(request._Type)) + " from client " + server.GetClientIPAddress(_pClient));
        server.Send("Error: Unhandled message type.", _pClient);
        break;
    }

    if (shouldSave) {
        Logger::getInstance().log(LogLevel::INFO, "Saving metadata state.");
        // Using global constants defined in metaserver.h for paths
        metadataManager.saveMetadata("file_metadata.dat", "node_registry.dat");
    }
    } catch (const Networking::NetworkException& ne) {
        Logger::getInstance().log(LogLevel::ERROR, "Network error in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(ne.what()));
        // server.DisconnectClient(_pClient); // Or similar cleanup
    } catch (const std::runtime_error& re) {
        Logger::getInstance().log(LogLevel::ERROR, "Runtime error (e.g., deserialization) in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(re.what()));
        server.Send("Error: Malformed message.", _pClient); // Optional: inform client
    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::ERROR, "Generic exception in HandleClientConnection for " + server.GetClientIPAddress(_pClient) + ": " + std::string(e.what()));
    }
}

int main()
{
    // Initialize logger for the metaserver
    try {
        Logger::init("metaserver.log", LogLevel::INFO); 
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Logger initialization failed for metaserver: " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Metaserver starting up...");
    // Load metadata at startup
    // Using global constants defined in metaserver.h for paths
    Logger::getInstance().log(LogLevel::INFO, "Loading metadata from file_metadata.dat and node_registry.dat");
    metadataManager.loadMetadata("file_metadata.dat", "node_registry.dat");

    if (server.ServerIsRunning()) // ServerIsRunning is true if socket setup succeeded.
    {
        Logger::getInstance().log(LogLevel::INFO, "Metaserver is running and listening on port " + std::to_string(server.GetPort()));
        // FileSystem fileSystem; // Using FileSystem from filesystem.h to manage file operations -- This seems unused here.
        while (true)
        {
            try {
                // Accept a client connection
                Networking::ClientConnection client = server.Accept(); // This blocks until a connection is made.
                Logger::getInstance().log(LogLevel::INFO, "Accepted new client connection from " + server.GetClientIPAddress(client));
                std::thread clientThread(HandleClientConnection, client);
                clientThread.detach();
                Logger::getInstance().log(LogLevel::DEBUG, "Detached thread to handle client " + server.GetClientIPAddress(client));

                // Periodically check for dead nodes (simplified for now)
                // In a production system, this would be handled by a separate timer thread
                // or integrated into an event loop more cleanly.
                // metadataManager.checkForDeadNodes(); 
                // If checkForDeadNodes modifies data and doesn't save itself, save here:
                // metadataManager.saveMetadata(FILE_METADATA_PATH, NODE_REGISTRY_PATH);
                // std::this_thread::sleep_for(std::chrono::seconds(NODE_TIMEOUT_SECONDS / 2)); // Example check interval
            } catch (const Networking::NetworkException& ne) {
                Logger::getInstance().log(LogLevel::ERROR, "Network exception in main server loop: " + std::string(ne.what()));
                // Depending on severity, might need to decide if server can continue.
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in main server loop: " + std::string(e.what()));
                // May need to gracefully shutdown or attempt recovery.
                break; // Example: exit loop on fatal error.
            } catch (...) {
                Logger::getInstance().log(LogLevel::FATAL, "Unknown unhandled exception in main server loop.");
                break; // Example: exit loop on fatal error.
            }
        }
    } else {
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (ServerIsRunning is false).");
    }
    Logger::getInstance().log(LogLevel::INFO, "Metaserver shutting down.");
    return 0;
}
