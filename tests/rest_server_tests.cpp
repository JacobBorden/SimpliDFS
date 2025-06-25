#include "httplib.h"
#include <boost/asio.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <thread>

#ifndef REST_SERVER_DISABLE_MAIN
#define REST_SERVER_DISABLE_MAIN
#endif
#include "../src/rest_server.cpp"

static std::string base64url_encode(const unsigned char *data, size_t len) {
  std::string out((len + 2) / 3 * 4, '\0');
  int out_len =
      EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]), data, len);
  out.resize(out_len);
  for (char &c : out) {
    if (c == '+')
      c = '-';
    else if (c == '/')
      c = '_';
  }
  while (!out.empty() && out.back() == '=')
    out.pop_back();
  return out;
}

static std::string make_token(const std::string &secret) {
  const std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
  const std::string payload = "{\"sub\":\"test\"}";
  std::string enc_header = base64url_encode(
      reinterpret_cast<const unsigned char *>(header.c_str()), header.size());
  std::string enc_payload = base64url_encode(
      reinterpret_cast<const unsigned char *>(payload.c_str()), payload.size());
  std::string signing_input = enc_header + "." + enc_payload;
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), secret.data(), secret.size(),
       reinterpret_cast<const unsigned char *>(signing_input.data()),
       signing_input.size(), digest, &len);
  std::string enc_sig = base64url_encode(digest, len);
  return signing_input + "." + enc_sig;
}

TEST(RestServer, TokenRotation) {
  int port1 = 15080;
  int port2 = 15081;
  std::string secret1 = "firstsecret";
  std::string secret2 = "secondsecret";

  boost::asio::io_context ioc1{1};
  RestServer srv1{ioc1, static_cast<unsigned short>(port1), secret1};
  srv1.run();
  std::thread t1([&]() { ioc1.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  httplib::Client cli1("localhost", port1);
  std::string token1 = make_token(secret1);
  auto res = cli1.Get("/snapshot", {{"Authorization", "Bearer " + token1}});
  ASSERT_TRUE(res != nullptr);
  EXPECT_NE(res->status, 401);

  ioc1.stop();
  t1.join();
  ioc1.reset();

  boost::asio::io_context ioc2{1};
  RestServer srv2{ioc2, static_cast<unsigned short>(port2), secret2};
  srv2.run();
  std::thread t2([&]() { ioc2.run(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  httplib::Client cli2("localhost", port2);
  res = cli2.Get("/snapshot", {{"Authorization", "Bearer " + token1}});
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);

  std::string token2 = make_token(secret2);
  res = cli2.Get("/snapshot", {{"Authorization", "Bearer " + token2}});
  ASSERT_TRUE(res != nullptr);
  EXPECT_NE(res->status, 401);

  ioc2.stop();
  t2.join();
}
