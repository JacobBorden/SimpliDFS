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
public: // New public methods for processing specific commands
    Message processWriteFileRequest(const Message& reqMsg) {
        Logger& logger = Logger::getInstance();
        logger.log(LogLevel::INFO, "[Node " + nodeName + "] Processing WriteFile request for " + reqMsg._Filename);

        // The content to write is in reqMsg._Content if sent by ReplicateFileCommand
        // or reqMsg._Data if sent by FUSE adapter (simpli_write)
        // The previous diff standardized on _Content for ReplicateFileCommand's writeFileMsg.
        // The FUSE adapter's simpli_write sends it in both _Data and _Content.
        // Let's prioritize _Content if available, else _Data.
        const std::string& data_to_write = !reqMsg._Content.empty() ? reqMsg._Content : reqMsg._Data;

        bool success = fileSystem.writeFile(reqMsg._Filename, data_to_write);

        Message response_msg;
        response_msg._Filename = reqMsg._Filename;
        response_msg._Type = MessageType::WriteResponse;

        if (success) {
            logger.log(LogLevel::INFO, "[Node " + nodeName + "] File '" + reqMsg._Filename + "' written successfully, " + std::to_string(data_to_write.length()) + " bytes.");
            response_msg._ErrorCode = 0;
            response_msg._Size = data_to_write.length();
        } else {
            logger.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to write file '" + reqMsg._Filename + "'.");
            response_msg._ErrorCode = EIO;
            response_msg._Size = 0;
        }
        return response_msg;
    }

    Message processReadFileRequest(const Message& reqMsg) {
        Logger& logger = Logger::getInstance();
        logger.log(LogLevel::INFO, "[Node " + nodeName + "] Processing ReadFile request for " + reqMsg._Filename);

        Message response_msg;
        response_msg._Type = MessageType::ReadFileResponse;
        response_msg._Filename = reqMsg._Filename;

        if (!fileSystem.fileExists(reqMsg._Filename)) {
            logger.log(LogLevel::WARN, "[Node " + nodeName + "] File '" + reqMsg._Filename + "' not found for ReadFile request.");
            response_msg._ErrorCode = ENOENT;
            response_msg._Data = "";
            response_msg._Size = 0;
        } else {
            // Assuming offset and size for partial reads are in reqMsg._Offset and reqMsg._Size
            // Current FileSystem::readFile reads the whole file. For partial reads, FileSystem needs enhancement.
            // For now, stick to whole file reads as per existing FileSystem::readFile behavior.
            std::string content = fileSystem.readFile(reqMsg._Filename);
            logger.log(LogLevel::INFO, "[Node " + nodeName + "] Successfully read file '" + reqMsg._Filename + "', size: " + std::to_string(content.length()));
            response_msg._ErrorCode = 0;
            response_msg._Data = content;
            response_msg._Size = content.length();
        }
        return response_msg;
    }

    // For ReplicateFileCommand, the Networking::Client to the target node is passed for testability
    Message processReplicateFileCommand(const Message& reqMsg, Networking::Client& clientToTargetNode) {
        Logger& logger = Logger::getInstance();
        std::string filenameToReplicate = reqMsg._Filename;
        std::string targetNodeAddressStr = reqMsg._NodeAddress;
        logger.log(LogLevel::INFO, "[Node " + nodeName + "] Processing ReplicateFileCommand for file '" + filenameToReplicate + "' to send to " + targetNodeAddressStr);

        Message response_to_metaserver; // This is the ack to the Metaserver
        response_to_metaserver._Type = MessageType::ReplicateFileCommand; // Echo type
        response_to_metaserver._ErrorCode = 0; // Assume success initially
        response_to_metaserver._Filename = filenameToReplicate;

        std::string fileContent = fileSystem.readFile(filenameToReplicate);
        if (fileContent.empty() && !fileSystem.fileExists(filenameToReplicate)) {
            logger.log(LogLevel::ERROR, "[Node " + nodeName + "] File '" + filenameToReplicate + "' not found for replication.");
            response_to_metaserver._ErrorCode = ENOENT;
            return response_to_metaserver;
        }

        try {
            size_t colon_pos = targetNodeAddressStr.find(':');
            if (colon_pos == std::string::npos) {
                logger.log(LogLevel::ERROR, "[Node " + nodeName + "] Invalid target node address format for replication: " + targetNodeAddressStr + " for file " + filenameToReplicate);
                response_to_metaserver._ErrorCode = EINVAL;
                return response_to_metaserver;
            }
            std::string targetIP = targetNodeAddressStr.substr(0, colon_pos);
            int targetPort = std::stoi(targetNodeAddressStr.substr(colon_pos + 1));

            // Use the passed-in clientToTargetNode (already constructed with targetIP, targetPort by caller)
            logger.log(LogLevel::INFO, "[Node " + nodeName + "] Connecting to target node " + targetNodeAddressStr + " to replicate file '" + filenameToReplicate + "' (using provided client).");

            if (!clientToTargetNode.Connect()) { // Assumes client is already set up with host/port
                logger.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to connect (returned false) to target node " + targetNodeAddressStr + " for file " + filenameToReplicate);
                response_to_metaserver._ErrorCode = EHOSTUNREACH;
            } else {
                Message writeFileMsg;
                writeFileMsg._Type = MessageType::WriteFile;
                writeFileMsg._Filename = filenameToReplicate;
                writeFileMsg._Content = fileContent;
                writeFileMsg._Size = fileContent.length();
                writeFileMsg._Offset = 0;

                std::string serializedWriteMsg = Message::Serialize(writeFileMsg);
                if (!clientToTargetNode.Send(serializedWriteMsg.c_str())) {
                    logger.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to send (returned false) WriteFile message to target node " + targetNodeAddressStr + " for file " + filenameToReplicate);
                    response_to_metaserver._ErrorCode = EIO;
                } else {
                    logger.log(LogLevel::INFO, "[Node " + nodeName + "] Successfully sent file '" + filenameToReplicate + "' to " + targetNodeAddressStr);
                    // Optional: Wait for confirmation from target node if protocol changes
                }
                clientToTargetNode.Disconnect();
            }
        } catch (const Networking::NetworkException& ne) {
            logger.log(LogLevel::ERROR, "[Node " + nodeName + "] NetworkException while sending file '" + filenameToReplicate + "' to " + targetNodeAddressStr + ": " + std::string(ne.what()));
            response_to_metaserver._ErrorCode = EIO;
        } catch (const std::exception& e) {
            logger.log(LogLevel::ERROR, "[Node " + nodeName + "] Exception while processing ReplicateFileCommand for '" + filenameToReplicate + "': " + std::string(e.what()));
            response_to_metaserver._ErrorCode = EINVAL;
        }
        return response_to_metaserver;
    }

    Message processReceiveFileCommand(const Message& reqMsg) {
        Logger& logger = Logger::getInstance();
        std::string filenameToReceive = reqMsg._Filename;
        std::string sourceNodeAddress = reqMsg._NodeAddress;
        logger.log(LogLevel::INFO, "[Node " + nodeName + "] Processing ReceiveFileCommand for file '" + filenameToReceive + "' from source " + sourceNodeAddress + ". Ready to receive.");

        Message response_msg;
        response_msg._Type = MessageType::ReceiveFileCommand;
        response_msg._ErrorCode = 0;
        response_msg._Filename = filenameToReceive;
        return response_msg;
    }

    Message processDeleteFileRequest(const Message& reqMsg) {
        Logger& logger = Logger::getInstance();
        logger.log(LogLevel::INFO, "[Node " + nodeName + "] Processing DeleteFile request for " + reqMsg._Filename);
        bool success = fileSystem.deleteFile(reqMsg._Filename);

        Message response_msg; // Response to Metaserver (if protocol requires one)
        response_msg._Type = MessageType::DeleteFile; // Echo type, or a specific DeleteFileResponse
        response_msg._Filename = reqMsg._Filename;

        if (success) {
            logger.log(LogLevel::INFO, "[Node " + nodeName + "] File '" + reqMsg._Filename + "' deleted successfully.");
            response_msg._ErrorCode = 0;
        } else {
            logger.log(LogLevel::WARN, "[Node " + nodeName + "] Failed to delete file '" + reqMsg._Filename + "' (not found or other error).");
            response_msg._ErrorCode = ENOENT;
        }
        // Current protocol does not expect a response from Node to Metaserver for DeleteFile.
        // So, this response_msg might not be sent. If it were, it's prepared.
        return response_msg; // This return is for consistency, but might not be sent by handleClient
    }

    Message processUnknownRequest(const Message& reqMsg) {
        Logger::getInstance().log(LogLevel::WARN, "[Node " + nodeName + "] Processing unknown message type: " + std::to_string(static_cast<int>(reqMsg._Type)));
        Message err_response_msg;
        err_response_msg._Type = reqMsg._Type;
        err_response_msg._ErrorCode = ENOSYS;
        return err_response_msg;
    }

private: // Keep handleClient, listenForRequests, etc. private or protected if base class
    void handleClient(Networking::ClientConnection client) {
        try {
            Logger& logger_hc = Logger::getInstance(); // Get logger instance for handleClient
            std::vector<char> request_vector = server.Receive(client);
            if (request_vector.empty()) {
                // Use logger instead of std::cerr
                logger_hc.log(LogLevel::WARN, "[Node " + nodeName + "] Received empty data from client " + server.GetClientIPAddress(client));
                return;
            }
            std::string request_str(request_vector.begin(), request_vector.end());
            Message message = Message::Deserialize(request_str);

            switch (message._Type) {
                case MessageType::WriteFile: {
                    bool success = fileSystem.writeFile(message._Filename, message._Content);
                    // Use logger for actions
                    logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Received WriteFile for " + message._Filename);
                    bool success = fileSystem.writeFile(message._Filename, message._Content); // Assuming _Content has data from FUSE adapter

                    Message response_msg;
                    response_msg._Filename = message._Filename;
                    if (success) {
                        logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] File '" + message._Filename + "' written successfully.");
                        response_msg._ErrorCode = 0;
                        response_msg._Size = message._Content.length(); // Confirm bytes written
                        // response_msg._Type = MessageType::FileWritten; // Or a generic success response
                    } else {
                        logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to write file '" + message._Filename + "'.");
                        response_msg._ErrorCode = EIO; // Generic I/O error
                        response_msg._Size = 0;
                    }
                    // Ensure a consistent response type if FUSE adapter expects it e.g. WriteResponse
                    response_msg._Type = MessageType::WriteResponse; // Using WriteResponse as defined in message.h
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent WriteFile response for " + message._Filename);
                    break;
                }
                case MessageType::ReadFile: { // Request from FUSE adapter to this node
                    logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Received ReadFile request for " + message._Filename + " from FUSE adapter.");
                    Message response_msg;
                    response_msg._Type = MessageType::ReadFileResponse; // Use the new specific response type
                    response_msg._Filename = message._Filename;

                    // fileSystem.readFile returns empty string if file not found or if actual file is empty.
                    // fileSystem.fileExists can distinguish.
                    if (!fileSystem.fileExists(message._Filename)) {
                        logger_hc.log(LogLevel::WARN, "[Node " + nodeName + "] File '" + message._Filename + "' not found for ReadFile request.");
                        response_msg._ErrorCode = ENOENT;
                        response_msg._Data = "";
                        response_msg._Size = 0;
                    } else {
                        std::string content = fileSystem.readFile(message._Filename);
                        // It's possible readFile itself could have an error, though current FileSystem doesn't show it
                        // For now, assume empty content means empty file if it exists.
                        logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Successfully read file '" + message._Filename + "', size: " + std::to_string(content.length()));
                        response_msg._ErrorCode = 0;
                        response_msg._Data = content;
                        response_msg._Size = content.length();
                    }
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent ReadFileResponse for " + message._Filename);
                    break;
                }
                // Note: The case for MessageType::RemoveFile has been removed. 
                // It was using fileSystem.writeFile with empty content, which is not a true delete.
                // MessageType::DeleteFile is handled below and uses fileSystem.deleteFile.
                // If MessageType::RemoveFile is a distinct, valid message type that should exist,
                // it needs to be re-added to the MessageType enum in message.h.
                // For now, assuming it was superseded by DeleteFile.
                case MessageType::ReplicateFileCommand: {
                    Logger& logger = Logger::getInstance();
                    std::string filenameToReplicate = message._Filename;
                    std::string targetNodeAddressStr = message._NodeAddress; // This is IP:PORT of the NEW node
                    // std::string thisNodeIDAsSource = message._Content; // This is the ID of THIS node (the source)

                    logger.log(LogLevel::INFO, "[Node " + nodeName + "] Received ReplicateFileCommand for file '" + filenameToReplicate + "' to send to " + targetNodeAddressStr);

                    std::string fileContent = fileSystem.readFile(filenameToReplicate);

                    // Standardize to use logger_hc
                    // fileSystem.fileExists is a good addition
                    if (fileContent.empty() && !fileSystem.fileExists(filenameToReplicate)) {
                        logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] File '" + filenameToReplicate + "' not found for replication.");
                        // Send a structured error response back to Metaserver
                        Message err_response_msg;
                        err_response_msg._Type = MessageType::ReplicateFileCommand; // Echo type or specific error type
                        err_response_msg._ErrorCode = ENOENT;
                        err_response_msg._Filename = filenameToReplicate;
                        server.Send(Message::Serialize(err_response_msg).c_str(), client);
                        logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent ReplicateFileCommand error response for " + filenameToReplicate);
                    } else {
                        // File read success (or it's an empty file which is valid)
                        try {
                            size_t colon_pos = targetNodeAddressStr.find(':');
                            if (colon_pos == std::string::npos) {
                                logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Invalid target node address format for replication: " + targetNodeAddressStr + " for file " + filenameToReplicate);
                                Message err_response_msg;
                                err_response_msg._Type = MessageType::ReplicateFileCommand;
                                err_response_msg._ErrorCode = EINVAL; // Invalid argument
                                server.Send(Message::Serialize(err_response_msg).c_str(), client);
                                break;
                            }
                            std::string targetIP = targetNodeAddressStr.substr(0, colon_pos);
                            int targetPort = std::stoi(targetNodeAddressStr.substr(colon_pos + 1));

                            Networking::Client clientToTargetNode(targetIP, targetPort);
                            logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Connecting to target node " + targetNodeAddressStr + " to replicate file '" + filenameToReplicate + "'");

                            Message ack_response_msg; // Prepare positive ack to Metaserver
                            ack_response_msg._Type = MessageType::ReplicateFileCommand; // Echo type
                            ack_response_msg._ErrorCode = 0;
                            ack_response_msg._Filename = filenameToReplicate;

                            if (!clientToTargetNode.Connect()) {
                                logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to connect (returned false) to target node " + targetNodeAddressStr + " for file " + filenameToReplicate);
                                ack_response_msg._ErrorCode = EHOSTUNREACH; // Update error code for MS
                            } else {
                                Message writeFileMsg; // Message to send to target data node
                                writeFileMsg._Type = MessageType::WriteFile;
                                writeFileMsg._Filename = filenameToReplicate;
                                writeFileMsg._Content = fileContent;
                                writeFileMsg._Size = fileContent.length();
                                writeFileMsg._Offset = 0;

                                std::string serializedWriteMsg = Message::Serialize(writeFileMsg);
                                if (!clientToTargetNode.Send(serializedWriteMsg.c_str())) {
                                    logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Failed to send (returned false) WriteFile message to target node " + targetNodeAddressStr + " for file " + filenameToReplicate);
                                    ack_response_msg._ErrorCode = EIO; // Update error code for MS
                                } else {
                                    logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Successfully sent file '" + filenameToReplicate + "' to " + targetNodeAddressStr);
                                    // Optionally wait for a response from the target node if protocol requires
                                    // For now, fire and forget, and send success to Metaserver.
                                }
                                clientToTargetNode.Disconnect();
                            }
                            server.Send(Message::Serialize(ack_response_msg).c_str(), client);
                            logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent ReplicateFileCommand final response for " + filenameToReplicate);

                        } catch (const Networking::NetworkException& ne) {
                            logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] NetworkException while sending file '" + filenameToReplicate + "' to " + targetNodeAddressStr + ": " + std::string(ne.what()));
                            Message err_response_msg;
                            err_response_msg._Type = MessageType::ReplicateFileCommand;
                            err_response_msg._ErrorCode = EIO; // Generic I/O for network issues with target
                            server.Send(Message::Serialize(err_response_msg).c_str(), client);
                        } catch (const std::exception& e) { // Catches std::stoi, etc.
                            logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Exception while processing ReplicateFileCommand for '" + filenameToReplicate + "': " + std::string(e.what()));
                            Message err_response_msg;
                            err_response_msg._Type = MessageType::ReplicateFileCommand;
                            err_response_msg._ErrorCode = EINVAL; // Or a more generic internal error
                            server.Send(Message::Serialize(err_response_msg).c_str(), client);
                        }
                    }
                    break;
                }
                case MessageType::ReceiveFileCommand: {
                    // Logger& logger = Logger::getInstance(); // logger_hc already available
                    std::string filenameToReceive = message._Filename;
                    std::string sourceNodeAddress = message._NodeAddress;

                    logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Received ReceiveFileCommand for file '" + filenameToReceive + "' from source " + sourceNodeAddress + ". Ready to receive.");

                    Message response_msg;
                    response_msg._Type = MessageType::ReceiveFileCommand; // Echo type
                    response_msg._ErrorCode = 0;
                    response_msg._Filename = filenameToReceive;
                    server.Send(Message::Serialize(response_msg).c_str(), client);
                    logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent ReceiveFileCommand acknowledgement for " + filenameToReceive);
                    break;
                }
                case MessageType::DeleteFile: { // Request from Metaserver
                    logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] Received DeleteFile request for " + message._Filename);
                    bool success = fileSystem.deleteFile(message._Filename);
                    Message response_msg;
                    // Decide on a response type, perhaps DeleteFileResponse or just an Ack. For now, echo DeleteFile.
                    response_msg._Type = MessageType::DeleteFile;
                    response_msg._Filename = message._Filename;

                    if (success) {
                        logger_hc.log(LogLevel::INFO, "[Node " + nodeName + "] File '" + message._Filename + "' deleted successfully.");
                        response_msg._ErrorCode = 0;
                    } else {
                        logger_hc.log(LogLevel::WARN, "[Node " + nodeName + "] Failed to delete file '" + message._Filename + "' (not found or other error).");
                        response_msg._ErrorCode = ENOENT; // Or EIO if it was another error
                    }
                    // server.Send(Message::Serialize(response_msg).c_str(), client); // Send response to Metaserver
                    // logger_hc.log(LogLevel::DEBUG, "[Node " + nodeName + "] Sent DeleteFile response for " + message._Filename);
                    // The original code for DeleteFile did not send a response. Keep it that way unless specified.
                    // The STUB comments suggest a response was planned. For now, no response to MS from DeleteFile.
                    break;
                }
                default: {
                    logger_hc.log(LogLevel::WARN, "[Node " + nodeName + "] Received unknown message type: " + std::to_string(static_cast<int>(message._Type)));
                    // Send a generic error or specific "unknown type" response
                    Message err_response_msg;
                    err_response_msg._Type = message._Type; // Echo type
                    err_response_msg._ErrorCode = ENOSYS; // Function not implemented / Unknown type
                    server.Send(Message::Serialize(err_response_msg).c_str(), client);
                    break;
                }
            }
        } catch (const std::runtime_error& e) { // Catching more specific runtime_error from Deserialize
            // Use logger instead of std::cerr
            logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Error deserializing message or runtime issue in handleClient for client " + server.GetClientIPAddress(client) + ": " + e.what());
            // Optionally, send an error response to the client if appropriate and connection is valid
            // server.Send("Error: Malformed message received.", client);
        } catch (const std::exception& e) { // Catching other general exceptions
            // Use logger instead of std::cerr
            logger_hc.log(LogLevel::ERROR, "[Node " + nodeName + "] Generic exception in handleClient for client " + server.GetClientIPAddress(client) + ": " + e.what());
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
            Logger& logger_smm = Logger::getInstance(); // Get logger instance for sendMessageToMetadataManager
            if (response_vector.empty()) {
                logger_smm.log(LogLevel::WARN, "[Node " + nodeName + "] Received empty response from MetadataManager for message type " + std::to_string(static_cast<int>(message._Type)));
            } else {
                std::string response(response_vector.begin(), response_vector.end());
                logger_smm.log(LogLevel::DEBUG, "[Node " + nodeName + "] Response from MetadataManager: " + response);
                // Further deserialize and check response message if needed by protocol for specific messages.
            }
        } catch (const Networking::NetworkException& ne) {
             Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Network error sending message type " + std::to_string(static_cast<int>(message._Type)) + " to MetadataManager: " + ne.what());
        } catch (const std::exception& e) { // Catching other potential exceptions
             Logger::getInstance().log(LogLevel::ERROR, "[Node " + nodeName + "] Generic error sending message type " + std::to_string(static_cast<int>(message._Type)) + " to MetadataManager: " + e.what());
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