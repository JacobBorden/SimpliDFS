/**
 * @file node.h
 * @brief Defines the Node class for the SimpliDFS distributed file system.
 *
 * The Node class represents a storage node in the system. It is responsible
 * for:
 * - Listening for incoming requests from clients or other nodes.
 * - Handling file operations (read, write, delete) by interacting with its
 * local FileSystem.
 * - Registering with the MetadataManager.
 * - Periodically sending heartbeats to the MetadataManager.
 * - Interacting with the NetworkingLibrary (currently stubbed/conceptual) for
 * communication.
 */

#include "utilities/client.h"
#include "utilities/filesystem.h"
#include "utilities/message.h"
#include "utilities/networkexception.h"
#include "utilities/server.h"
#include <chrono> // Required for std::chrono
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector> // Required for std::vector

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
  std::string nodeName;      ///< Unique identifier for this node.
  Networking::Server server; ///< Server instance from NetworkingLibrary to
                             ///< listen for incoming connections.
  FileSystem fileSystem;     ///< Local file system manager for this node.
  void verifyLoop(const std::string &metaAddr, int metaPort,
                  int intervalSeconds) {
    while (true) {
      for (const auto &f : fileSystem.listFiles()) {
        if (!fileSystem.verifyFileIntegrity(f)) {
          std::cout << "[NODE " << nodeName << "] Detected corruption in " << f
                    << std::endl;

          Message req;
          req._Type = MessageType::GetFileNodeLocationsRequest;
          req._Path = f;
          try {
            Networking::Client mc(metaAddr.c_str(), metaPort);
            mc.Send(Message::Serialize(req).c_str());
            std::vector<char> respVec = mc.Receive();
            mc.Disconnect();
            if (!respVec.empty()) {
              Message resp = Message::Deserialize(
                  std::string(respVec.begin(), respVec.end()));
              std::stringstream ss(resp._Data);
              std::string addr;
              while (std::getline(ss, addr, ',')) {
                if (addr.empty())
                  continue;
                if (addr == "127.0.0.1:" + std::to_string(server.GetPort()))
                  continue;
                std::string ip = addr.substr(0, addr.find(':'));
                int port = std::stoi(addr.substr(addr.find(':') + 1));
                try {
                  Networking::Client sc(ip.c_str(), port);
                  Message rm;
                  rm._Type = MessageType::ReadFile;
                  rm._Filename = f;
                  sc.Send(Message::Serialize(rm).c_str());
                  std::vector<char> fileVec = sc.Receive();
                  sc.Disconnect();
                  if (!fileVec.empty()) {
                    std::string data(fileVec.begin(), fileVec.end());
                    fileSystem.writeFile(f, data);
                    std::cout << "[NODE " << nodeName << "] Healed " << f
                              << " from " << addr << std::endl;
                    break;
                  }
                } catch (...) {
                }
              }
            }
          } catch (...) {
            std::cerr << "[NODE " << nodeName
                      << "] Error contacting metaserver for healing"
                      << std::endl;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
    }
  }

public:
  /**
   * @brief Checks if a file exists on the node's local filesystem.
   * @param filename The name of the file to check.
   * @return True if the file exists, false otherwise.
   */
  bool
  checkFileExistsOnNode(const std::string &filename) const; // Made const again

  /**
   * @brief Constructs a Node object.
   * @param name The unique name (identifier) for this node.
   * @param port The port number on which this node's server should listen.
   */
  Node(const std::string &name, int port) : nodeName(name), server(port) {}

  /**
   * @brief Starts the node's operations.
   * This includes starting the server to listen for requests and initiating
   * the periodic heartbeat sender.
   */
  void start() {
    std::cout << "Node " << nodeName << ": Attempting to start server on port "
              << server.GetPort() << std::endl;
    if (!this->server.startListening()) {
      std::cerr << "Node " << nodeName
                << ": CRITICAL - Failed to start server listening on port "
                << server.GetPort() << "." << std::endl;
      // Depending on desired behavior, could throw an exception or set an error
      // state. For now, just log and don't start other threads.
      return;
    }
    std::cout << "Node " << nodeName << ": Server started successfully on port "
              << server.GetPort() << std::endl;

    // Start the node's server in a separate thread to listen to requests
    std::thread serverThread(&Node::listenForRequests, this);
    serverThread.detach();
    std::cout << "Node " << nodeName << ": Listener thread detached."
              << std::endl;

    // Start the heartbeat thread
    // Replace "127.0.0.1" and 50505 with actual MetadataManager IP and port if
    // different Heartbeat interval is 10 seconds
    std::thread heartbeatThread(&Node::sendHeartbeatPeriodically, this,
                                "127.0.0.1", 50505, 10);
    heartbeatThread.detach();
    std::cout << "Node " << nodeName << ": Heartbeat thread detached."
              << std::endl;

    std::thread verifyThread(&Node::verifyLoop, this, "127.0.0.1", 50505, 60);
    verifyThread.detach();
    std::cout << "Node " << nodeName << ": Verifier thread detached."
              << std::endl;

    // Original log: std::cout << "Node " << nodeName << " started on port " <<
    // server.GetPort() << std::endl; This is now logged above after successful
    // startListening()
  }

  /**
   * @brief Registers this node with the MetadataManager.
   * Sends a RegisterNode message containing this node's name, address
   * (placeholder), and listening port.
   * @param metadataManagerAddress The IP address or hostname of the
   * MetadataManager.
   * @param metadataManagerPort The port number of the MetadataManager.
   */
  void registerWithMetadataManager(const std::string &metadataManagerAddress,
                                   int metadataManagerPort) {
    Message msg;
    msg._Type = MessageType::RegisterNode;
    msg._Filename =
        this->nodeName; // Using _Filename to carry the node identifier
    msg._NodeAddress = "127.0.0.1"; // Placeholder for node's actual address
    msg._NodePort = this->server.GetPort(); // Node's listening port

    // This function would make a network call to the MetadataManager
    sendMessageToMetadataManager(metadataManagerAddress, metadataManagerPort,
                                 msg);
    std::cout << "Node " << nodeName
              << " attempting to register with MetadataManager at "
              << metadataManagerAddress << ":" << metadataManagerPort
              << std::endl;
  }

  /**
   * @brief Listens for incoming client connections and spawns threads to handle
   * them. This method runs in a loop as long as the server is running.
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
   * @param client The ClientConnection object representing the connected
   * client.
   * @note This method uses the local FileSystem to perform file operations.
   *       Error handling for message deserialization and network operations is
   * included.
   */
  void handleClient(Networking::ClientConnection client) {
    try {
      std::vector<char> request_vector = server.Receive(client);
      if (request_vector.empty()) {
        std::cerr << "Node " << nodeName << " received empty data."
                  << std::endl;
        return;
      }
      std::string request_str(request_vector.begin(), request_vector.end());
      Message message = Message::Deserialize(request_str);

      switch (message._Type) {
      case MessageType::WriteFile: {
        bool success = false;
        if (message._Content
                .empty()) { // Treat empty content write as create file
          success = fileSystem.createFile(message._Filename);
          if (success) {
            server.Send(("File " + message._Filename + " created successfully.")
                            .c_str(),
                        client);
          } else {
            // createFile logs if it already exists or other errors.
            // Check if it already exists to send a different success message
            // for idempotency.
            if (fileSystem.readFile(message._Filename).empty() &&
                !fileSystem.getXattr(message._Filename, "user.cid").empty()) {
              // This is tricky: readFile returns "" for non-existent or for
              // truly empty (but existing) files. If it has xattrs, it must
              // exist. A file created by createFile will have empty content and
              // no xattrs. A proper fileSystem.fileExists() would be better.
              // For now, if createFile fails, assume it might be due to already
              // existing. Let's refine: if createFile returns false, it means
              // it already existed (as per its log). This can be treated as
              // success for "ensure exists".
              Logger::getInstance().log(
                  LogLevel::INFO,
                  "Node " + nodeName +
                      ": WriteFile with empty content for existing file " +
                      message._Filename + " (treated as success).");
              server.Send(("File " + message._Filename +
                           " (already exists) processed successfully.")
                              .c_str(),
                          client);
              success = true; // Idempotent success
            } else {
              server.Send(("Error: Unable to create file " + message._Filename +
                           " (may already exist or other issue).")
                              .c_str(),
                          client);
            }
          }
        } else { // Non-empty content, try to write (will fail if file doesn't
                 // exist)
          success = fileSystem.writeFile(message._Filename, message._Content);
          if (success) {
            server.Send(("File " + message._Filename + " written successfully.")
                            .c_str(),
                        client);
          } else {
            server.Send(
                ("Error: Unable to write file " + message._Filename + ".")
                    .c_str(),
                client);
          }
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
      // It was using fileSystem.writeFile with empty content, which is not a
      // true delete. MessageType::DeleteFile is handled below and uses
      // fileSystem.deleteFile. If MessageType::RemoveFile is a distinct, valid
      // message type that should exist, it needs to be re-added to the
      // MessageType enum in message.h. For now, assuming it was superseded by
      // DeleteFile.
      case MessageType::ReplicateFileCommand: {
        std::string filenameToReplicate = message._Filename;
        std::string targetNodeAddress = message._NodeAddress;
        std::string sourceNodeForConfirmation =
            message
                ._Content; // Original source node ID for logging/confirmation
        std::cout << "[NODE " << nodeName << "] Replicating "
                  << filenameToReplicate << " to " << targetNodeAddress
                  << std::endl;
        std::string data = fileSystem.readFile(filenameToReplicate);
        if (!data.empty()) {
          std::string ip =
              targetNodeAddress.substr(0, targetNodeAddress.find(':'));
          int port = std::stoi(
              targetNodeAddress.substr(targetNodeAddress.find(':') + 1));
          try {
            Networking::Client c(ip.c_str(), port);
            Message w;
            w._Type = MessageType::WriteFile;
            w._Filename = filenameToReplicate;
            w._Content = data;
            c.Send(Message::Serialize(w).c_str());
            (void)c.Receive();
            c.Disconnect();
          } catch (...) {
            std::cerr << "[NODE " << nodeName << "] Replication to "
                      << targetNodeAddress << " failed" << std::endl;
          }
        }
        server.Send("Replication command processed.", client);
        break;
      }
      case MessageType::ReceiveFileCommand: {
        std::string filenameToReceive = message._Filename;
        std::string sourceNodeAddress = message._NodeAddress;
        std::string targetNodeForConfirmation =
            message
                ._Content; // Original target node ID for logging/confirmation
        std::cout << "[NODE " << nodeName << "] Receiving " << filenameToReceive
                  << " from " << sourceNodeAddress << std::endl;
        std::string ip =
            sourceNodeAddress.substr(0, sourceNodeAddress.find(':'));
        int port = std::stoi(
            sourceNodeAddress.substr(sourceNodeAddress.find(':') + 1));
        try {
          Networking::Client sc(ip.c_str(), port);
          Message r;
          r._Type = MessageType::ReadFile;
          r._Filename = filenameToReceive;
          sc.Send(Message::Serialize(r).c_str());
          std::vector<char> vec = sc.Receive();
          sc.Disconnect();
          if (!vec.empty()) {
            std::string d(vec.begin(), vec.end());
            fileSystem.writeFile(filenameToReceive, d);
          }
        } catch (...) {
          std::cerr << "[NODE " << nodeName << "] Failed to receive file from "
                    << sourceNodeAddress << std::endl;
        }
        server.Send("Receive command processed.", client);
        break;
      }
      case MessageType::DeleteFile: {
        std::cout << "[NODE " << nodeName << "] Received DeleteFile for "
                  << message._Filename << std::endl;
        bool success = fileSystem.deleteFile(message._Filename);
        if (success) {
          std::cout << "[NODE " << nodeName << "] File " << message._Filename
                    << " deleted successfully." << std::endl;
          // STUB: server.Send(("File " + message._Filename + "
          // deleted.").c_str(), client);
          std::cout << "[NODE " << nodeName
                    << "_STUB] Sent delete confirmation to metaserver/client."
                    << std::endl;
        } else {
          std::cout << "[NODE " << nodeName << "] Error: Unable to delete file "
                    << message._Filename << " (not found or other error)."
                    << std::endl;
          // STUB: server.Send(("Error deleting " + message._Filename).c_str(),
          // client);
          std::cout << "[NODE " << nodeName
                    << "_STUB] Sent delete error to metaserver/client."
                    << std::endl;
        }
        break;
      }
      default: {
        server.Send("Unknown request type.", client);
        break;
      }
      }
    } catch (const std::runtime_error
                 &e) { // Catching more specific runtime_error from Deserialize
      std::cerr
          << "Error deserializing message or runtime issue in handleClient: "
          << e.what() << std::endl;
      // Optionally, send an error response to the client if appropriate
      // server.Send("Error: Malformed message received.", client);
    } catch (const std::exception &e) { // Catching other general exceptions
      std::cerr << "Error handling client: " << e.what() << std::endl;
    }
  }

  /**
   * @brief Sends a message to the MetadataManager.
   * Serializes the given Message object and sends it using the
   * Networking::Client.
   * @param metadataManagerAddress The IP address or hostname of the
   * MetadataManager.
   * @param metadataManagerPort The port number of the MetadataManager.
   * @param message The Message object to send.
   * @note This method currently uses STUBs for actual network sending via
   * NetworkingLibrary.
   */
  void sendMessageToMetadataManager(const std::string &metadataManagerAddress,
                                    int metadataManagerPort,
                                    const Message &message) {
    try {
      Networking::Client client(metadataManagerAddress.c_str(),
                                metadataManagerPort);
      std::string serializedMessage = Message::Serialize(message);
      client.Send(serializedMessage.c_str());
      std::vector<char> response_vector = client.Receive();
      if (response_vector.empty()) {
        std::cout << "Node " << nodeName
                  << " received empty response from MetadataManager."
                  << std::endl;
        // Handle empty response, maybe log or retry
      }
      // Only construct string if not empty, or handle empty string case
      std::string response = "";
      if (!response_vector.empty()) {
        response = std::string(response_vector.begin(), response_vector.end());
      }
      // Suppress noisy cout during tests, consider logging framework if complex
      // logs needed std::cout << "Response from MetadataManager: " << response
      // << std::endl;
    } catch (const Networking::NetworkException &ne) {
      std::cerr << "Network error sending message to MetadataManager: "
                << ne.what() << std::endl;
    } catch (const std::exception &e) { // Catching other potential exceptions
      std::cerr << "Error sending message to MetadataManager: " << e.what()
                << std::endl;
    }
  }

private:
  /**
   * @brief Periodically sends heartbeat messages to the MetadataManager.
   * This method runs in a separate thread.
   * @param metadataManagerAddress The IP address or hostname of the
   * MetadataManager.
   * @param metadataManagerPort The port number of the MetadataManager.
   * @param intervalSeconds The interval in seconds at which to send heartbeats.
   */
  void sendHeartbeatPeriodically(const std::string &metadataManagerAddress,
                                 int metadataManagerPort, int intervalSeconds) {
    while (true) { // Or use a running flag to control the loop
      Message msg;
      msg._Type = MessageType::Heartbeat;
      msg._Filename =
          this->nodeName; // Using _Filename to carry the node identifier

      // This function would make a network call to the MetadataManager
      sendMessageToMetadataManager(metadataManagerAddress, metadataManagerPort,
                                   msg);
      // std::cout << "Node " << nodeName << " sent heartbeat to
      // MetadataManager." << std::endl; // Optional: for debugging

      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
    }
  }
};