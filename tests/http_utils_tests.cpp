#include "utilities/http.hpp"
#include <gtest/gtest.h>

TEST(HttpUtils, RoundTripRequest) {
  HTTP::HTTPREQUEST req;
  req.method = HTTP::HttpMethod::POST;
  req.uri = "/foo";
  req.protocol = "HTTP/1.1";
  req.headers["Content-Type"] = "text/plain";
  req.body = "hello";

  std::string raw = HTTP::GenerateHttpRequestString(req);
  auto parsed = HTTP::ParseHttpRequest(raw);
  EXPECT_EQ(parsed.method, req.method);
  EXPECT_EQ(parsed.uri, req.uri);
  EXPECT_EQ(parsed.protocol, req.protocol);
  ASSERT_EQ(parsed.headers.at("Content-Type"), "text/plain");
  EXPECT_EQ(parsed.body, req.body);
}
