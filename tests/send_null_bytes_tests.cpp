#include "gtest/gtest.h"
#include "utilities/server.h"
#include "utilities/client.h"
#include <thread>

TEST(NetworkingNullByteTest, ClientServerBinarySend) {
    const int port = 12420;
    Networking::Server server(port);
    ASSERT_TRUE(server.startListening());

    Networking::ClientConnection conn{};
    std::thread serverThread([&]() {
        conn = server.Accept();
        std::vector<char> data = server.Receive(conn);
        std::string received(data.begin(), data.end());
        server.Send(received, conn);
        server.DisconnectClient(conn);
    });

    Networking::Client client("127.0.0.1", port);
    ASSERT_TRUE(client.IsConnected());
    std::string payload = std::string("hello\0world", 11);
    client.Send(payload);
    std::vector<char> respVec = client.Receive();
    std::string resp(respVec.begin(), respVec.end());
    EXPECT_EQ(resp, payload);
    client.Disconnect();
    serverThread.join();
    server.Shutdown();
}
