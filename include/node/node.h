/**
 * @file node.h
 * @brief Defines the Node class for the SimpliDFS distributed file system.
 * 
 * The Node class represents a storage node in the system. It is responsible for:
 * - Listening for incoming requests from clients or other nodes.
 * - Handling file operations (read, write, delete) by interacting with its local FileSystem.
 * - Registering with the MetadataManager.
 * - Periodically sending heartbeats to the MetadataManager.
 * - Interacting with the NetworkingLibrary (currently stubbed/conceptual) for communication.
 */

#include <iostream>
#include <string>
#include <vector> // Required for std::vector
#include "utilities/filesystem.h"
#include "utilities/message.h"
#include "utilities/server.h"
#include "utilities/client.h"
#include "utilities/networkexception.h"
#include <thread>
#include <chrono> // Required for std::chrono

/**
 * @brief Represents a storage node in the SimpliDFS system.
 * 
 * Each Node instance runs a server to listen for requests, manages a local
 * FileSystem instance for storing file data, and communicates with the
 * MetadataManager for registration and heartbeats. It also handles commands
 * from the MetadataManager for file replication.
 */
class Node {
private:
    std::string nodeName;       ///< Unique identifier for this node.
    Networking::Server server;  ///< Server instance from NetworkingLibrary to listen for incoming connections.
    FileSystem fileSystem;      ///< Local file system manager for this node.

public:
    /**
     * @brief Constructs a Node object.
     * @param name The unique name (identifier) for this node.
     * @param port The port number on which this node's server should listen.
     */
    Node(const std::string& name, int port) : nodeName(name), server(port) {}

    /**
     * @brief Starts the node's operations.
     * This includes starting the server to listen for requests and initiating
     * the periodic heartbeat sender.
     */
    void start() {
        // Start the node's server in a separate thread to listen to requests
        std::thread serverThread(&Node::listenForRequests, this);
        serverThread.detach();

        // Start the heartbeat thread
        // Replace "127.0.0.1" and 50505 with actual MetadataManager IP and port if different
        // Heartbeat interval is 10 seconds
        std::thread heartbeatThread(&Node::sendHeartbeatPeriodically, this, "127.0.0.1", 50505, 10);
        heartbeatThread.detach();

        std::cout << "Node " << nodeName << " started on port " << server.GetPort() << std::endl;
    }

    /**
     * @brief Registers this node with the MetadataManager.
     * Sends a RegisterNode message containing this node's name, address (placeholder), 
     * and listening port.
     * @param metadataManagerAddress The IP address or hostname of the MetadataManager.
     * @param metadataManagerPort The port number of the MetadataManager.
     */
    void registerWithMetadataManager(const std::string& metadataManagerAddress, int metadataManagerPort) {
        Message msg;
        msg._Type = MessageType::RegisterNode;
        msg._Filename = this->nodeName; // Using _Filename to carry the node identifier
        msg._NodeAddress = "127.0.0.1"; // Placeholder for node's actual address
        msg._NodePort = this->server.GetPort(); // Node's listening port

        // This function would make a network call to the MetadataManager
        sendMessageToMetadataManager(metadataManagerAddress, metadataManagerPort, msg);
        std::cout << "Node " << nodeName << " attempting to register with MetadataManager at "
                  << metadataManagerAddress << ":" << metadataManagerPort << std::endl;
    }

    /**
     * @brief Listens for incoming client connections and spawns threads to handle them.
     * This method runs in a loop as long as the server is running.
     */
    void listenForRequests() {
        while (server.ServerIsRunning()) {
            Networking::ClientConnection client = server.Accept();
            std::thread clientThread(&Node::handleClient, this, client);
            clientThread.detach();
        }
    }

    /**
     * @brief Handles an individual client connection.
     * Receives a message, deserializes it, and processes it based on its type.
     * Supported message types include WriteFile, ReadFile, DeleteFile, 
     * ReplicateFileCommand, and ReceiveFileCommand.
     * @param client The ClientConnection object representing the connected client.
     * @note This method uses the local FileSystem to perform file operations.
     *       Error handling for message deserialization and network operations is included.
     */
    void handleClient(Networking::ClientConnection client) {
        try {
            std::vector<char> request_vector = server.Receive(client);
            if (request_vector.empty()) {
                std::cerr << "Node " << nodeName << " received empty data." << std::endl;
                return;
            }
            std::string request_str(request_vector.begin(), request_vector.end());
            Message message = Message::Deserialize(request_str);

            switch (message._Type) {
                case MessageType::WriteFile: {
                    bool success = fileSystem.writeFile(message._Filename, message._Content);
                    if (success) {
                        server.Send(("File " + message._Filename + " written successfully.").c_str(), client);
                    } else {
                        server.Send(("Error: Unable to write file " + message._Filename + ".").c_str(), client);
                    }
                    break;
                }
                case MessageType::ReadFile: {
                    std::string content = fileSystem.readFile(message._Filename);
                    if (!content.empty()) {
                        server.Send(content.c_str(), client);
                    } else {
                        server.Send("Error: File not found.", client);
                    }
                    break;
                }
                // Note: The case for MessageType::RemoveFile has been removed. 
                // It was using fileSystem.writeFile with empty content, which is not a true delete.
                // MessageType::DeleteFile is handled below and uses fileSystem.deleteFile.
                // If MessageType::RemoveFile is a distinct, valid message type that should exist,
                // it needs to be re-added to the MessageType enum in message.h.
                // For now, assuming it was superseded by DeleteFile.
                case MessageType::ReplicateFileCommand: {
                    std::string filenameToReplicate = message._Filename;
                    std::string targetNodeAddress = message._NodeAddress;
                    std::string sourceNodeForConfirmation = message._Content; // Original source node ID for logging/confirmation
                    std::cout << "[NODE " << nodeName << "] Received ReplicateFileCommand for " << filenameToReplicate 
                              << " to " << targetNodeAddress 
                              << " (Original source: " << sourceNodeForConfirmation << ")" << std::endl;
                    std::cout << "[NODE " << nodeName << "_STUB] Reading file " << filenameToReplicate 
                              << " and simulating send to " << targetNodeAddress << std::endl;
                    // STUB: actual_content = fileSystem.readFile(filenameToReplicate);
                    // STUB: This node would then connect to targetNodeAddress and send the file
                    // Example: Networking::Client clientToTarget(targetNodeAddress_ip, targetNodeAddress_port);
                    // clientToTarget.Send(actual_content);
                    server.Send("Replication command received.", client); // Acknowledge receipt
                    break;
                }
                case MessageType::ReceiveFileCommand: {
                    std::string filenameToReceive = message._Filename;
                    std::string sourceNodeAddress = message._NodeAddress;
                    std::string targetNodeForConfirmation = message._Content; // Original target node ID for logging/confirmation
                    std::cout << "[NODE " << nodeName << "] Received ReceiveFileCommand for " << filenameToReceive 
                              << " from " << sourceNodeAddress 
                              << " (Original target: " << targetNodeForConfirmation << ")" << std::endl;
                    std::cout << "[NODE " << nodeName << "_STUB] Simulating receive of " << filenameToReceive 
                              << " from " << sourceNodeAddress << " and writing to local filesystem." << std::endl;
                    // STUB: This node would expect a connection from sourceNodeAddress or initiate if needed
                    // Example: std::string received_content = server.Receive(client_from_source_node);
                    // fileSystem.writeFile(filenameToReceive, received_content);
                    server.Send("Receive command acknowledged.", client); // Acknowledge receipt
                    break;
                }
                case MessageType::DeleteFile: {
                    std::cout << "[NODE " << nodeName << "] Received DeleteFile for " << message._Filename << std::endl;
                    bool success = fileSystem.deleteFile(message._Filename);
                    if (success) {
                        std::cout << "[NODE " << nodeName << "] File " << message._Filename << " deleted successfully." << std::endl;
                        // STUB: server.Send(("File " + message._Filename + " deleted.").c_str(), client);
                        std::cout << "[NODE " << nodeName << "_STUB] Sent delete confirmation to metaserver/client." << std::endl;
                    } else {
                        std::cout << "[NODE " << nodeName << "] Error: Unable to delete file " << message._Filename << " (not found or other error)." << std::endl;
                        // STUB: server.Send(("Error deleting " + message._Filename).c_str(), client);
                         std::cout << "[NODE " << nodeName << "_STUB] Sent delete error to metaserver/client." << std::endl;
                    }
                    break;
                }
                default: {
                    server.Send("Unknown request type.", client);
                    break;
                }
            }
        } catch (const std::runtime_error& e) { // Catching more specific runtime_error from Deserialize
            std::cerr << "Error deserializing message or runtime issue in handleClient: " << e.what() << std::endl;
            // Optionally, send an error response to the client if appropriate
            // server.Send("Error: Malformed message received.", client);
        } catch (const std::exception& e) { // Catching other general exceptions
            std::cerr << "Error handling client: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Sends a message to the MetadataManager.
     * Serializes the given Message object and sends it using the Networking::Client.
     * @param metadataManagerAddress The IP address or hostname of the MetadataManager.
     * @param metadataManagerPort The port number of the MetadataManager.
     * @param message The Message object to send.
     * @note This method currently uses STUBs for actual network sending via NetworkingLibrary.
     */
    void sendMessageToMetadataManager(const std::string& metadataManagerAddress, int metadataManagerPort, const Message& message) {
        try {
            Networking::Client client(metadataManagerAddress.c_str(), metadataManagerPort);
            std::string serializedMessage = Message::Serialize(message);
            client.Send(serializedMessage.c_str());
            std::vector<char> response_vector = client.Receive();
            if (response_vector.empty()) {
                std::cout << "Node " << nodeName << " received empty response from MetadataManager." << std::endl;
                // Handle empty response, maybe log or retry
            }
            // Only construct string if not empty, or handle empty string case
            std::string response = "";
            if (!response_vector.empty()){
                response = std::string(response_vector.begin(), response_vector.end());
            }
            std::cout << "Response from MetadataManager: " << response << std::endl;
        } catch (const Networking::NetworkException& ne) {
             std::cerr << "Network error sending message to MetadataManager: " << ne.what() << std::endl;
        } catch (const std::exception& e) { // Catching other potential exceptions
            std::cerr << "Error sending message to MetadataManager: " << e.what() << std::endl;
        }
    }

private:
    /**
     * @brief Periodically sends heartbeat messages to the MetadataManager.
     * This method runs in a separate thread.
     * @param metadataManagerAddress The IP address or hostname of the MetadataManager.
     * @param metadataManagerPort The port number of the MetadataManager.
     * @param intervalSeconds The interval in seconds at which to send heartbeats.
     */
    void sendHeartbeatPeriodically(const std::string& metadataManagerAddress, int metadataManagerPort, int intervalSeconds) {
        while (true) { // Or use a running flag to control the loop
            Message msg;
            msg._Type = MessageType::Heartbeat;
            msg._Filename = this->nodeName; // Using _Filename to carry the node identifier

            // This function would make a network call to the MetadataManager
            sendMessageToMetadataManager(metadataManagerAddress, metadataManagerPort, msg);
            // std::cout << "Node " << nodeName << " sent heartbeat to MetadataManager." << std::endl; // Optional: for debugging

            std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
        }
    }
};