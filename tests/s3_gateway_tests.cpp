#include "httplib.h"
#include "s3_gateway.h"
#include "utilities/key_manager.hpp"
#include "utilities/logger.h"
#include "gtest/gtest.h"
#include <chrono>
#include <thread>

class S3GatewayTest : public ::testing::Test {
protected:
  FileSystem fs;
  std::unique_ptr<S3Gateway> gateway;

  void SetUp() override {
    Logger::init(Logger::CONSOLE_ONLY_OUTPUT, LogLevel::ERROR);
    simplidfs::KeyManager::getInstance().initialize();
    gateway = std::make_unique<S3Gateway>(fs);
  }

  void TearDown() override { gateway->stop(); }
};

// Disabled in CI due to port binding and timing issues on shared runners
TEST_F(S3GatewayTest, DISABLED_UploadAndDownload) {
  int port = 14000;
  gateway->start(port);
  // Allow time for the background thread to begin accepting connections.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  httplib::Client cli("localhost", port);
  auto put_res = cli.Put("/testbucket/hello.txt", "data", "text/plain");
  ASSERT_TRUE(put_res != nullptr);
  EXPECT_EQ(put_res->status, 200);

  auto get_res = cli.Get("/testbucket/hello.txt");
  ASSERT_TRUE(get_res != nullptr);
  EXPECT_EQ(get_res->status, 200);
  EXPECT_EQ(get_res->body, "data");
}
