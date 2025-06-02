#pragma once

#include <gmock/gmock.h>
#include "utilities/client.h" // For Networking::Client interface, if needed, or just mimic
#include "utilities/networkexception.h" // For Networking::NetworkException
#include <string>
#include <vector>

// Standalone Mock for Networking::Client as its methods are likely not virtual
class MockNetClient {
public:
    MockNetClient() = default;
    // Mimic constructor that might take host/port, or have separate connect
    MOCK_METHOD(bool, CreateClientTCPSocket, (const char* host, int port), ());
    MOCK_METHOD(bool, ConnectClientSocket, (), ());
    MOCK_METHOD(bool, Send, (const char* data), ());
    MOCK_METHOD(std::vector<char>, Receive, (), ());
    MOCK_METHOD(void, Disconnect, (), ());
    MOCK_METHOD(bool, IsConnected, (), (const));

    // Helper to simulate a connected state for tests if needed
    void SetConnectedState(bool connected) {
        ON_CALL(*this, IsConnected).WillByDefault(testing::Return(connected));
    }
};

// If Networking::Client had virtual methods, it would be:
// class MockNetClient : public Networking::Client {
// public:
//     MockNetClient() : Networking::Client("dummy", 0) {} // Call base constructor
//     MOCK_METHOD(bool, ConnectClientSocket, (), (override));
//     MOCK_METHOD(bool, Send, (const char* data), (override));
//     MOCK_METHOD(std::vector<char>, Receive, (), (override));
//     MOCK_METHOD(void, Disconnect, (), (override));
//     MOCK_METHOD(bool, IsConnected, (), (const, override));
//     // Note: CreateClientTCPSocket is tricky to mock if it's part of constructor or setup before Connect.
//     // Typically, mock the state after construction.
// };

#endif // TESTS_MOCKS_MOCK_NETWORKING_H
