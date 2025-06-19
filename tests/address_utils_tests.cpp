#include <gtest/gtest.h>
#include "utilities/address_utils.h"

TEST(AddressUtils, ParsesValidAddress) {
    std::string ip;
    int port = 0;
    ASSERT_TRUE(parseAddressPort("127.0.0.1:5000", ip, port));
    EXPECT_EQ(ip, "127.0.0.1");
    EXPECT_EQ(port, 5000);
}

TEST(AddressUtils, RejectsInvalidAddress) {
    std::string ip;
    int port = 0;
    EXPECT_FALSE(parseAddressPort("invalid", ip, port));
}
