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
    serverThread.detach(); // Detach as client might connect before accept is called or vice-versa

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server a moment to start listening

    Networking::Client client("127.0.0.1", 12346);
    ASSERT_TRUE(client.IsConnected());
    client.Disconnect();
    ASSERT_FALSE(client.IsConnected());
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
    serverThread.detach();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); 

    Networking::Client client("127.0.0.1", testPort);
    ASSERT_TRUE(client.IsConnected());
    client.Send(testMessage);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Give time for message to be processed

    client.Disconnect();
    server.Shutdown();
    
    // Wait for server thread to complete its receive operation
    // A more robust way would be to use condition variables or futures.
    // For simplicity in a test, a join or a longer sleep might be used, 
    // but detached thread makes join impossible directly here.
    // This check might be flaky if timing is off.
    // Consider joining serverThread if it's not detached or use promises/futures.
    // For now, relying on sleep and then checking.
    if (serverThread.joinable()) serverThread.join(); // Should not happen if detached
    
    ASSERT_EQ(receivedMessage, testMessage);
}
/*
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
*/
TEST(NetworkingTest, MultipleClientsConnect) {
    const int testPort = 12349;
    Networking::Server server(testPort);
    ASSERT_TRUE(server.InitServer());

    std::vector<std::thread> clientThreads;
    const int numClients = 5;

    std::thread serverAcceptThread([&]() {
        for (int i = 0; i < numClients; ++i) {
            Networking::ClientConnection c = server.Accept();
            ASSERT_TRUE(c.clientSocket != 0); 
            // Optionally, interact with client then disconnect
             server.DisconnectClient(c);
        }
    });
    serverAcceptThread.detach(); // Detach to allow clients to connect

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give server a moment

    for (int i = 0; i < numClients; ++i) {
        clientThreads.emplace_back([&]() {
            Networking::Client client("127.0.0.1", testPort);
            ASSERT_TRUE(client.IsConnected());
            // Perform some action or just connect and disconnect
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Simulate some work
            client.Disconnect();
            ASSERT_FALSE(client.IsConnected());
        });
    }

    for (auto& t : clientThreads) {
        t.join();
    }
    
    // Ensure server accept thread also finishes if it wasn't detached or has a clear end condition
    // If detached, we assume it has processed all clients based on sleeps and client joins
    // For robustness, use a counter or condition variable.
    if(serverAcceptThread.joinable()) serverAcceptThread.join(); 

    server.Shutdown();
    // Check server.getClients().size() if available and appropriate, after disconnections it should be 0
    // For this test, primary check is that all clients connected and disconnected successfully.
}
