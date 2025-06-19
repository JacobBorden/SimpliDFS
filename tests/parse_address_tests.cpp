#include "node/node.h"
#include <gtest/gtest.h>

/**
 * @brief Verify that a well-formed address parses correctly.
 */
TEST(NodeUtils, ParseAddressPortValid) {
    std::string ip;
    int port = 0;
    bool ok = Node::parseAddressPort("127.0.0.1:1234", ip, port);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ip, "127.0.0.1");
    EXPECT_EQ(port, 1234);
}

/**
 * @brief Ensure invalid strings are rejected.
 */
TEST(NodeUtils, ParseAddressPortInvalid) {
    std::string ip;
    int port = 0;
    EXPECT_FALSE(Node::parseAddressPort("invalid", ip, port));
}
