// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "filesystem.h"
#include "message.h" // Including message for node communication
#include <sstream>
#include "metaserver.h"
#include "server.h"
#include "networkexception.h"
#include <thread>
#include <string> // Required for std::string constructor from vector iterators

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
        std::vector<char> received_vector = server.Receive(_pClient);
        if (received_vector.empty()) {
            // Handle empty receive, maybe client disconnected or sent no data
            std::cerr << "Received empty data from client." << std::endl;
            // Depending on server logic, might want to close connection or return
            return; 
        }
        std::string received_data_str(received_vector.begin(), received_vector.end());
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
        std::cout << "Sent registration confirmation to node " << request._Filename << std::endl; // Placeholder
        break;
    }
    case MessageType::Heartbeat:
    {
        metadataManager.processHeartbeat(request._Filename); // _Filename contains nodeIdentifier
        // For heartbeats, saving metadata might be too frequent.
        // Node liveness changes are saved by checkForDeadNodes if it's called and modifies state.
        // shouldSave = false; // Or true if every heartbeat should force a save of NodeInfo
        break;
    }
    case MessageType::DeleteFile: {
        std::cout << "[METASERVER] Received DeleteFile request for " << request._Filename << std::endl;
        metadataManager.removeFile(request._Filename); // This will trigger notifications
        server.Send("Delete command processed.", _pClient);
        std::cout << "[METASERVER_STUB] Sent DeleteFile command processed confirmation." << std::endl;
        shouldSave = true; // Ensure metadata is saved
        break;
    }
    // Add cases for other metadata-modifying operations like RemoveFile if they exist
    }

    if (shouldSave) {
        // Using global constants defined in metaserver.h for paths
        metadataManager.saveMetadata("file_metadata.dat", "node_registry.dat");
    }
    } catch (const Networking::NetworkException& ne) {
        std::cerr << "Network error in HandleClientConnection: " << ne.what() << std::endl;
        // server.DisconnectClient(_pClient); // Or similar cleanup
    } catch (const std::runtime_error& re) {
        std::cerr << "Runtime error (e.g., deserialization) in HandleClientConnection: " << re.what() << std::endl;
        server.Send("Error: Malformed message.", _pClient); // Optional: inform client
    }
}

int main()
{
    // Load metadata at startup
    // Using global constants defined in metaserver.h for paths
    metadataManager.loadMetadata("file_metadata.dat", "node_registry.dat");

    if (server.ServerIsRunning())
    {

        FileSystem fileSystem; // Using FileSystem from filesystem.h to manage file operations
        while (true)
        {
            // Accept a client connection

            Networking::ClientConnection client = server.Accept();
            std::thread clientThread(HandleClientConnection, client);
            clientThread.detach();

            // Periodically check for dead nodes (simplified for now)
            // In a production system, this would be handled by a separate timer thread
            // or integrated into an event loop more cleanly.
            // metadataManager.checkForDeadNodes(); 
            // If checkForDeadNodes modifies data and doesn't save itself, save here:
            // metadataManager.saveMetadata(FILE_METADATA_PATH, NODE_REGISTRY_PATH);
            // std::this_thread::sleep_for(std::chrono::seconds(NODE_TIMEOUT_SECONDS / 2)); // Example check interval
        }
    }

    return 0;
}
