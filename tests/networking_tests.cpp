#include "gtest/gtest.h"
#include "utilities/client.h"
#include "utilities/server.h"
#include "utilities/networkexception.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include "utilities/logger.h" // Add this include
#include <cstdio>   // For std::remove
#include <string>   // For std::to_string in TearDown

// Basic test fixture for networking tests if needed, or just use TEST directly.
class NetworkingTest : public ::testing::Test {
protected:
    // Optional: Set up resources shared by tests in this fixture
    void SetUp() override {
        try {
            Logger::init("networking_tests.log", LogLevel::DEBUG);
        } catch (const std::exception& e) {
            // Handle or log if SetUp itself fails critically
        }
    }

    // Optional: Clean up shared resources
    void TearDown() override {
        try {
            Logger::init("dummy_net_cleanup.log", LogLevel::DEBUG);
            std::remove("dummy_net_cleanup.log"); 
            std::remove("dummy_net_cleanup.log.1"); // Clean up potential rotated dummy log
        } catch (const std::runtime_error& e) { /* ignore */ }
        
        std::remove("networking_tests.log");
        for (int i = 1; i <= 5; ++i) {
            std::remove(("networking_tests.log." + std::to_string(i)).c_str());
        }
    }
};

TEST_F(NetworkingTest, ServerInitialization) { // Changed to TEST_F
    Networking::Server server(12345);
    ASSERT_TRUE(server.InitServer()); // Assuming InitServer is the main initialization step
    ASSERT_TRUE(server.ServerIsRunning()); // Or a similar check
    ASSERT_EQ(server.GetPort(), 12345);
    server.Shutdown(); // Ensure server is properly closed
}

TEST_F(NetworkingTest, ClientInitializationAndConnection) { // Changed to TEST_F
    Networking::Server server(12346);
    ASSERT_TRUE(server.InitServer());
    std::thread serverThread([&]() {
        server.Accept(); // Accept one connection
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Increased delay to ensure server is listening

    Networking::Client client("127.0.0.1", 12346);
    int retries = 0;
    while (!client.IsConnected() && retries < 20) { // More retries
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client = Networking::Client("127.0.0.1", 12346);
        retries++;
    }
    ASSERT_TRUE(client.IsConnected());
    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());
    if (serverThread.joinable()) serverThread.join();
    server.Shutdown();
}

TEST_F(NetworkingTest, ClientCannotConnectToNonListeningServer) { // Changed to TEST_F
    // Attempt to connect to a port where no server is listening
    Networking::Client client("127.0.0.1", 12340); // Some unlikely port
    ASSERT_FALSE(client.IsConnected()); 
}

TEST_F(NetworkingTest, SendAndReceiveClientToServer) { // Changed to TEST_F
    const int testPort = 12347;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    const char* testMessage = "Hello Server from Client";
    std::string receivedMessage;
    std::thread serverThread([&]() {
        Networking::ClientConnection clientConn = server.Accept();
        ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
        std::vector<char> data = server.Receive(clientConn);
        if (!data.empty()) {
            receivedMessage = std::string(data.begin(), data.end());
        }
        server.DisconnectClient(clientConn);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    Networking::Client client("127.0.0.1", testPort);
    ASSERT_TRUE(client.IsConnected());
    client.Send(testMessage);
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Give time for message to be processed
    client.Disconnect();
    if (serverThread.joinable()) serverThread.join(); // Join instead of detach
    server.Shutdown();
    ASSERT_EQ(receivedMessage, testMessage);
}

TEST_F(NetworkingTest, SendAndReceiveServerToClient) { // Changed to TEST_F
    const int testPort = 12348;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    const char* testMessage = "Hello Client from Server";
    std::string receivedMessage;

    std::thread clientThread([&]() {
        Networking::Client client("127.0.0.1", testPort);
        if (client.IsConnected()) {
            std::vector<char> data = client.Receive();
            if (!data.empty()) {
                receivedMessage = std::string(data.begin(), data.end());
            }
            client.Disconnect();
        }
    });
    
    Networking::ClientConnection clientConn = server.Accept();
    ASSERT_TRUE(clientConn.clientSocket != 0); // Check for valid client socket
    server.Send(testMessage, clientConn);
    
    clientThread.join(); // Wait for client to finish
    server.DisconnectClient(clientConn);
    server.Shutdown();
    
    ASSERT_EQ(receivedMessage, testMessage);
}

TEST_F(NetworkingTest, MultipleClientsConnect) { // Changed to TEST_F
    const int testPort = 12349;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::vector<std::thread> clientThreads;
    const int numClients = 5;
    std::atomic<int> connectedClients{0};

    std::thread serverAcceptThread([&]() {
        for (int i = 0; i < numClients; ++i) {
            Networking::ClientConnection c = server.Accept();
            ASSERT_TRUE(c.clientSocket != 0);
            server.DisconnectClient(c);
            connectedClients++;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Ensure server is listening

    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([testPort]() {
            Networking::Client client("127.0.0.1", testPort);
            int retries = 0;
            while (!client.IsConnected() && retries < 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                client = Networking::Client("127.0.0.1", testPort);
                retries++;
            }
            ASSERT_TRUE(client.IsConnected());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            client.Disconnect();
            ASSERT_FALSE(client.IsConnected());
        });
    }

    for (auto& t : clientThreads) {
        t.join();
    }
    if (serverAcceptThread.joinable()) serverAcceptThread.join(); // Join instead of detach
    server.Shutdown();
    ASSERT_EQ(connectedClients, numClients);
}
