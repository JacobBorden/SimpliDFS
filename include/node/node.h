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
            Message response_msg; // Prepare a response message

            switch (message._Type) {
                case MessageType::CreateFile: { // Handler for CreateFile from Metaserver
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received CreateFile for " + message._Filename);
                    // Assuming CreateFile from metaserver means create an empty file.
                    // fileSystem.createFile might be more semantic if it exists and guarantees empty.
                    // writeFile with empty content also works.
                    bool success = fileSystem.writeFile(message._Filename, "");
                    response_msg._Type = MessageType::FileCreated; // Or a generic Response type
                    response_msg._Filename = message._Filename;
                    if (success) {
                        Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] File " + message._Filename + " created successfully.");
                        response_msg._ErrorCode = 0;
                    } else {
                        // This might happen if file already exists and writeFile cannot overwrite,
                        // or other FS errors.
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Error creating file " + message._Filename + " (it might already exist or FS error).");
                        response_msg._ErrorCode = EEXIST; // Or a generic error
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                case MessageType::WriteFile: { // Existing WriteFile, likely from a direct client not metaserver
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received WriteFile for " + message._Filename);
                    bool success = fileSystem.writeFile(message._Filename, message._Content);
                    // Response for WriteFile is not clearly defined in original code, adding a simple one
                    response_msg._Type = MessageType::FileWritten; // Or a generic Response type
                    response_msg._Filename = message._Filename;
                    if (success) {
                        Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] File " + message._Filename + " written successfully.");
                        response_msg._ErrorCode = 0;
                    } else {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Error writing file " + message._Filename);
                        response_msg._ErrorCode = EIO; // General I/O error
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                case MessageType::ReadFile: { // Existing ReadFile, likely from a direct client
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received ReadFile for " + message._Filename);
                    std::string content = fileSystem.readFile(message._Filename);
                    response_msg._Type = MessageType::FileRead; // Or a generic Response type
                    response_msg._Filename = message._Filename;
                    if (!content.empty() || fileSystem.fileExists(message._Filename)) { // Check fileExists for truly empty files
                        Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] File " + message._Filename + " read, size: " + std::to_string(content.length()));
                        response_msg._Content = content;
                        response_msg._ErrorCode = 0;
                    } else {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Error reading file " + message._Filename + " (not found).");
                        response_msg._ErrorCode = ENOENT;
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                // Note: The case for MessageType::RemoveFile has been removed. 
                // It was using fileSystem.writeFile with empty content, which is not a true delete.
                // MessageType::DeleteFile is handled below and uses fileSystem.deleteFile.
                // If MessageType::RemoveFile is a distinct, valid message type that should exist,
                // it needs to be re-added to the MessageType enum in message.h.
                // For now, assuming it was superseded by DeleteFile.
                case MessageType::ReplicateFileCommand: { // From Metaserver
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received ReplicateFileCommand for file " + message._Filename + " to target node ID " + message._Content + " at address " + message._NodeAddress);

                    std::string filenameToReplicate = message._Filename;
                    std::string targetNodeFullAddress = message._NodeAddress; // Expected "ip:port"
                    std::string targetNodeID_for_logging = message._Content;

                    response_msg._Type = MessageType::ReplicateFileCommand; // Prepare response to Metaserver
                    response_msg._Filename = filenameToReplicate;
                    response_msg._ErrorCode = 0; // Default to success for the command processing itself

                    std::string localFileContent = fileSystem.readFile(filenameToReplicate);
                    if (!fileSystem.fileExists(filenameToReplicate)) { // Check if file exists, readFile might return empty for non-existent
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Local file " + filenameToReplicate + " not found.");
                        response_msg._ErrorCode = ENOENT; // Error: Source file not found
                        server.Send(Message::Serialize(response_msg).c_str(), client); // Send error status to Metaserver
                        break;
                    }
                    Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] ReplicateFileCommand: Read local file " + filenameToReplicate + ", size: " + std::to_string(localFileContent.length()) + " bytes.");

                    size_t colon_pos = targetNodeFullAddress.rfind(':');
                    if (colon_pos == std::string::npos) {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Invalid target node address format: " + targetNodeFullAddress);
                        response_msg._ErrorCode = EINVAL; // Error: Invalid address format
                        server.Send(Message::Serialize(response_msg).c_str(), client); // Send error status to Metaserver
                        break;
                    }

                    std::string target_ip = targetNodeFullAddress.substr(0, colon_pos);
                    int target_port = -1;
                    try {
                        target_port = std::stoi(targetNodeFullAddress.substr(colon_pos + 1));
                    } catch (const std::exception& e) {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Invalid target port in address " + targetNodeFullAddress + ": " + e.what());
                        response_msg._ErrorCode = EINVAL;
                        server.Send(Message::Serialize(response_msg).c_str(), client);
                        break;
                    }

                    if (target_port == -1) { // Should be caught by exception, but as a safeguard
                         Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Target port parsing failed for " + targetNodeFullAddress);
                         response_msg._ErrorCode = EINVAL;
                         server.Send(Message::Serialize(response_msg).c_str(), client);
                         break;
                    }

                    Networking::Client clientToTarget;
                    Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] ReplicateFileCommand: Attempting to connect to target node " + targetNodeID_for_logging + " at " + target_ip + ":" + std::to_string(target_port));
                    if (!clientToTarget.CreateClientTCPSocket(target_ip.c_str(), target_port) || !clientToTarget.ConnectClientSocket()) {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Failed to connect to target node " + targetNodeID_for_logging + " at " + targetNodeFullAddress);
                        response_msg._ErrorCode = EHOSTUNREACH; // Error: Target node unreachable
                        // No need to send to metaserver here, as this is part of the command execution, not command ack to meta.
                        // The original response_msg to metaserver will be sent at the end.
                    } else {
                        Message writeFileToTargetMsg;
                        writeFileToTargetMsg._Type = MessageType::WriteFile;
                        writeFileToTargetMsg._Filename = filenameToReplicate;
                        writeFileToTargetMsg._Content = localFileContent;

                        std::string serializedDataMsg = Message::Serialize(writeFileToTargetMsg);
                        Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] ReplicateFileCommand: Sending WriteFile message for " + filenameToReplicate + " to target " + targetNodeID_for_logging);
                        if (!clientToTarget.Send(serializedDataMsg.c_str())) {
                            Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Failed to send WriteFile message to target " + targetNodeID_for_logging);
                            response_msg._ErrorCode = EIO; // Error: Failed to send
                        } else {
                            // Optionally wait for and log response from target node's WriteFile handler
                            std::vector<char> target_resp_vec = clientToTarget.Receive();
                            if (target_resp_vec.empty()) {
                                Logger::getInstance().log(LogLevel::WARN, "[Node " + nodeName + "] ReplicateFileCommand: Received no/empty response from target " + targetNodeID_for_logging + " after sending file " + filenameToReplicate);
                            } else {
                                try {
                                    Message targetWriteResp = Message::Deserialize(std::string(target_resp_vec.begin(), target_resp_vec.end()));
                                    if (targetWriteResp._ErrorCode == 0) {
                                        Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] ReplicateFileCommand: Target node " + targetNodeID_for_logging + " confirmed successful write of " + filenameToReplicate);
                                    } else {
                                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] ReplicateFileCommand: Target node " + targetNodeID_for_logging + " reported error " + std::to_string(targetWriteResp._ErrorCode) + " on writing file " + filenameToReplicate);
                                        response_msg._ErrorCode = EIO; // Propagate as general I/O error for replication
                                    }
                                } catch(const std::exception& e) {
                                     Logger::getInstance().log(LogLevel::WARN, "[Node " + nodeName + "] ReplicateFileCommand: Exception deserializing WriteFile response from target " + targetNodeID_for_logging + ": " + e.what());
                                }
                            }
                        }
                        clientToTarget.Disconnect();
                    }
                    // Send response to Metaserver about processing the command
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                case MessageType::ReceiveFileCommand: { // From Metaserver
                     Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received ReceiveFileCommand for file " + message._Filename + " from source node ID " + message._Content + " at address " + message._NodeAddress + ". Ready to receive.");
                    // This node is the target. It just needs to be aware. The actual data comes via WriteFile.
                    response_msg._Type = MessageType::ReceiveFileCommand; // Echoing type
                    response_msg._ErrorCode = 0; // Acknowledge command received successfully
                    server.Send(Message::Serialize(response_msg).c_str(), client); // Acknowledge command to Metaserver
                    break;
                }
                case MessageType::NodeReadFileChunk: {
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received NodeReadFileChunk for " + message._Filename + " Offset: " + std::to_string(message._Offset) + " Size: " + std::to_string(message._Size));
                    response_msg._Type = MessageType::NodeReadFileChunkResponse;
                    response_msg._Filename = message._Filename;

                    std::string full_content = fileSystem.readFile(message._Filename);
                    if (!fileSystem.fileExists(message._Filename)) { // Check if file doesn't exist at all
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] NodeReadFileChunk: File " + message._Filename + " not found.");
                        response_msg._ErrorCode = ENOENT;
                        response_msg._Data = "";
                        response_msg._Size = 0;
                    } else {
                        // Handle offset and size for the read operation
                        int64_t offset = message._Offset;
                        uint64_t size_to_read = message._Size;

                        if (offset < 0) offset = 0; // Treat negative offset as 0

                        if (static_cast<uint64_t>(offset) >= full_content.length()) {
                            // Offset is at or beyond EOF
                            response_msg._Data = "";
                            response_msg._Size = 0;
                        } else {
                            uint64_t readable_length = full_content.length() - static_cast<uint64_t>(offset);
                            uint64_t actual_read_size = std::min(size_to_read, readable_length);
                            response_msg._Data = full_content.substr(static_cast<size_t>(offset), static_cast<size_t>(actual_read_size));
                            response_msg._Size = actual_read_size;
                        }
                        response_msg._ErrorCode = 0;
                        Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] NodeReadFileChunk: Read " + std::to_string(response_msg._Size) + " bytes from " + message._Filename);
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                case MessageType::NodeWriteFileChunk: {
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received NodeWriteFileChunk for " + message._Filename + " Offset: " + std::to_string(message._Offset) + " DataSize: " + std::to_string(message._Data.length()));
                    response_msg._Type = MessageType::NodeWriteFileChunkResponse;
                    response_msg._Filename = message._Filename;

                    std::string current_content;
                    if (fileSystem.fileExists(message._Filename)) {
                        current_content = fileSystem.readFile(message._Filename);
                    }

                    int64_t offset = message._Offset;
                    if (offset < 0) offset = 0; // Treat negative offset as 0

                    std::string new_content;
                    uint64_t u_offset = static_cast<uint64_t>(offset);

                    // Ensure buffer is large enough for write at offset
                    if (u_offset > current_content.length()) {
                        new_content.reserve(u_offset + message._Data.length());
                        new_content.append(current_content);
                        new_content.append(u_offset - current_content.length(), '\0'); // Pad with null bytes
                    } else {
                        new_content.reserve(current_content.length() - u_offset + message._Data.length());
                        new_content.append(current_content.substr(0, u_offset));
                    }

                    new_content.append(message._Data);

                    // If the write is not at the end of the original file, append the rest of the original file
                    if (u_offset + message._Data.length() < current_content.length()) {
                        new_content.append(current_content.substr(u_offset + message._Data.length()));
                    }

                    bool success = fileSystem.writeFile(message._Filename, new_content);
                    if (success) {
                        response_msg._Size = message._Data.length(); // Report bytes from request Data
                        response_msg._ErrorCode = 0;
                        Logger::getInstance().log(LogLevel::DEBUG, "[Node " + nodeName + "] NodeWriteFileChunk: Wrote " + std::to_string(response_msg._Size) + " bytes to " + message._Filename);
                    } else {
                        response_msg._Size = 0;
                        response_msg._ErrorCode = EIO; // General I/O error
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] NodeWriteFileChunk: Error writing to " + message._Filename);
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    break;
                }
                case MessageType::DeleteFile: { // From Metaserver
                    Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] Received DeleteFile for " + message._Filename);
                    bool success = fileSystem.deleteFile(message._Filename);
                    response_msg._Type = MessageType::FileRemoved; // Or a generic Response type
                    response_msg._Filename = message._Filename;
                    if (success) {
                        Logger::getInstance().log(LogLevel::INFO, "[Node " + nodeName + "] File " + message._Filename + " deleted successfully.");
                        response_msg._ErrorCode = 0;
                    } else {
                        Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Error deleting file " + message._Filename + " (not found or other error).");
                        response_msg._ErrorCode = ENOENT; // File not found is a common reason
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
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