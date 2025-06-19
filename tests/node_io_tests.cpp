#include <gtest/gtest.h>
#include "node/node.h"
#include "utilities/client.h"
#include "utilities/message.h"
#include <thread>
#include <chrono>
#include <filesystem>

// Helper to ensure the RBAC policy file is available in the test directory
static void prepare_rbac_policy() {
    namespace fs = std::filesystem;
    fs::path cwd = fs::current_path();
    fs::path policyDest = cwd / "rbac_policy.yaml";
    if (fs::exists(policyDest)) return; // Already present
    fs::path policySrc = cwd.parent_path().parent_path() / "rbac_policy.yaml";
    if (fs::exists(policySrc)) {
        fs::copy_file(policySrc, policyDest, fs::copy_options::overwrite_existing);
    }
}

/**
 * @brief Test writing and reading a file through Node's network interface.
 *
 * This test starts a Node instance, creates a file using a WriteFile message,
 * writes "Hello World" to it and then reads the content back to verify the
 * operation. This bypasses the FUSE layer entirely.
 */
TEST(NodeIOTests, WriteThenReadHelloWorld) {
    prepare_rbac_policy();
    const int nodePort = 12450;           // Port for the test node
    const std::string filename = "hello.txt";
    const std::string fileContent = "Hello World";

    // Start the node which internally launches its server thread
    Node node("testNode", nodePort);
    node.start();

    // Give the server a moment to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // --- Step 1: Create the file on the node ---
    {
        Networking::Client client("127.0.0.1", nodePort);
        ASSERT_TRUE(client.IsConnected());

        // Sending WriteFile with empty content triggers file creation
        Message createMsg;
        createMsg._Type = MessageType::WriteFile;
        createMsg._Filename = filename;
        createMsg._Content = "";
        client.Send(Message::Serialize(createMsg));
        std::vector<char> resp = client.Receive();
        client.Disconnect();
        ASSERT_FALSE(resp.empty()); // Expect some confirmation text
    }

    // --- Step 2: Write "Hello World" to the file ---
    {
        Networking::Client client("127.0.0.1", nodePort);
        ASSERT_TRUE(client.IsConnected());

        Message writeMsg;
        writeMsg._Type = MessageType::WriteFile;
        writeMsg._Filename = filename;
        writeMsg._Content = fileContent;
        client.Send(Message::Serialize(writeMsg));
        std::vector<char> resp = client.Receive();
        client.Disconnect();
        ASSERT_FALSE(resp.empty());
    }

    // --- Step 3: Read the file content back ---
    {
        Networking::Client client("127.0.0.1", nodePort);
        ASSERT_TRUE(client.IsConnected());

        Message readMsg;
        readMsg._Type = MessageType::ReadFile;
        readMsg._Filename = filename;
        client.Send(Message::Serialize(readMsg));
        std::vector<char> resp = client.Receive();
        client.Disconnect();
        std::string data(resp.begin(), resp.end());
        EXPECT_EQ(data, fileContent);
    }

    // Allow any background processing to complete before destruction
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

