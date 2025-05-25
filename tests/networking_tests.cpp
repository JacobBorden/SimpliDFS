#include "gtest/gtest.h"
#include "client.h"
#include "server.h"
#include "networkexception.h"
#include <thread>
#include <chrono>
#include <vector>
#include <string>

// Basic test fixture for networking tests if needed, or just use TEST directly.
class NetworkingTest : public ::testing::Test {
protected:
    // Optional: Set up resources shared by tests in this fixture
    void SetUp() override {
        // e.g., start a server instance if many tests need it
    }

    // Optional: Clean up shared resources
    void TearDown() override {
        // e.g., stop server instance
    }
};

TEST(NetworkingTest, ServerInitialization) {
    Networking::Server server(12345);
    ASSERT_TRUE(server.InitServer()); // Assuming InitServer is the main initialization step
    ASSERT_TRUE(server.ServerIsRunning()); // Or a similar check
    ASSERT_EQ(server.GetPort(), 12345);
    server.Shutdown(); // Ensure server is properly closed
}

TEST(NetworkingTest, ClientInitializationAndConnection) {
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

TEST(NetworkingTest, ClientCannotConnectToNonListeningServer) {
    // Attempt to connect to a port where no server is listening
    Networking::Client client("127.0.0.1", 12340); // Some unlikely port
    ASSERT_FALSE(client.IsConnected()); 
}

TEST(NetworkingTest, SendAndReceiveClientToServer) {
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

TEST(NetworkingTest, SendAndReceiveServerToClient) {
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

TEST(NetworkingTest, MultipleClientsConnect) {
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
