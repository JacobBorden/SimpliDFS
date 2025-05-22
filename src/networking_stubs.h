#pragma once
#include <string>
#include <vector>
#include <iostream> // For std::cout in stubs
#include <algorithm> // Required for std::remove for vector manipulation (potentially)

namespace Networking {
    // Forward declaration
    struct ClientConnection; 

    class Server {
    public:
        Server(int port = 0) : port_(port), is_running_(true) {
            // std::cout << "[STUB] Networking::Server created on port " << port_ << std::endl;
        }
        bool ServerIsRunning() const { return is_running_; }
        ClientConnection Accept(); // Definition below ClientConnection
        
        std::vector<char> Receive(ClientConnection& client) {
            // std::cout << "[STUB] Networking::Server attempting to receive data for client " << client.id << std::endl;
            if (client_messages_to_server.empty()) {
                 return std::vector<char>(); // Return empty vector if no message
            }
            // This is a simplification; real server would map messages to specific clients.
            // Here, any client can pick up the last message sent by *any* client to the server.
            std::string msg_str = client_messages_to_server.back();
            client_messages_to_server.pop_back(); // Not thread-safe for real use
            return std::vector<char>(msg_str.begin(), msg_str.end());
        }
        
        void Send(const char* data, ClientConnection& client) {
            // std::cout << "[STUB] Networking::Server sending data: \"" << data << "\" to client " << client.id << std::endl;
        }
        int GetPort() const { return port_; }

        // Test support: Store messages that clients "send" to server
        // Using inline static for C++17 compatibility to define in header
        inline static std::vector<std::string> client_messages_to_server;

    private:
        int port_;
        bool is_running_;
    };
    
    struct ClientConnection {
        int id; // Dummy identifier
        ClientConnection(int i = 0) : id(i) {} 

        // Make it possible to use in conditional expressions if needed
        operator bool() const { return id != 0; } // e.g. return true if id is not some default invalid value
    };

    // Definition of Server::Accept after ClientConnection is fully defined
    inline ClientConnection Server::Accept() {
        // std::cout << "[STUB] Networking::Server accepting a new client." << std::endl;
        static int next_client_id = 1; // Simple incrementing ID for stub
        return ClientConnection(next_client_id++);
    }
    
    class Client {
    public:
        Client(const char* address, int port) {
            // std::cout << "[STUB] Networking::Client created for " << address << ":" << port << std::endl;
        }
        void Send(const char* data) {
            // std::cout << "[STUB] Networking::Client sending data: \"" << data << "\"" << std::endl;
            // For testing, let's imagine client sends data which server might pick up
            // This needs to be thread-safe if multiple clients send concurrently.
            // For a stub, direct push_back is okay but not for real use without a mutex.
            Networking::Server::client_messages_to_server.push_back(std::string(data));
        }
        std::vector<char> Receive() {
            // std::cout << "[STUB] Networking::Client attempting to receive data." << std::endl;
            // Client receive is often more complex, depending on if it expects specific messages
            // For now, just return empty.
            return std::vector<char>(); 
        }
    };
} // namespace Networking
