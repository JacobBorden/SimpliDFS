// Node Implementation for SimpliDFS
// This code defines a node in a distributed file system (DFS) that interacts with the MetadataManager and other nodes.

#include <iostream>
#include <string>
#include "filesystem.h"
#include "message.h"
#include "client.h" // Networking client from the NetworkingLibrary
#include "server.h" // Networking server for listening to requests
#include <thread>

class Node {
private:
    std::string nodeName;
    Networking::Server server;
    FileSystem fileSystem; // Use the FileSystem class for managing files

public:
    Node(const std::string& name, int port) : nodeName(name), server(port) {}

    void start() {
        // Start the node's server in a separate thread to listen to requests
        std::thread serverThread(&Node::listenForRequests, this);
        serverThread.detach();
        std::cout << "Node " << nodeName << " started on port " << server.getPort() << std::endl;
    }

    void listenForRequests() {
        while (server.ServerIsRunning()) {
            Networking::ClientConnection client = server.Accept();
            std::thread clientThread(&Node::handleClient, this, client);
            clientThread.detach();
        }
    }

    void handleClient(Networking::ClientConnection client) {
        try {
            std::string request = &server.Receive(client)[0];
            Message message = DeserializeMessage(request);

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
                case MessageType::RemoveFile: {
                    bool success = fileSystem.writeFile(message._Filename, ""); // Assuming this removes the content
                    if (success) {
                        server.Send(("File " + message._Filename + " removed successfully.").c_str(), client);
                    } else {
                        server.Send("Error: File not found.", client);
                    }
                    break;
                }
                default: {
                    server.Send("Unknown request type.", client);
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error handling client: " << e.what() << std::endl;
        }
    }

    void sendMessageToMetadataManager(const std::string& metadataManagerAddress, int metadataManagerPort, const Message& message) {
        try {
            Networking::Client client(metadataManagerAddress.c_str(), metadataManagerPort);
            std::string serializedMessage = SerializeMessage(message);
            client.Send(serializedMessage.c_str());
            std::string response = &client.Receive()[0];
            std::cout << "Response from MetadataManager: " << response << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error sending message to MetadataManager: " << e.what() << std::endl;
        }
    }
};