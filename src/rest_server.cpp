#include "utilities/filesystem.h"
#include "utilities/http.hpp"
#include "utilities/json.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <cppcodec/base64_url_unpadded.hpp>
#include <sodium.h>
#include <string>
#include <vector>

using tcp = boost::asio::ip::tcp;
using base64 = cppcodec::base64_url_unpadded;

/**
 * @brief Minimal REST server with JWT authentication.
 */
class RestServer {
public:
  /**
   * @brief Construct server listening on the given port.
   * @param ioc  Boost.Asio I/O context.
   * @param port Port number to bind.
   * @param secret HMAC secret for JWT verification.
   */
  RestServer(boost::asio::io_context &ioc, unsigned short port,
             const std::string &secret)
      : acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), secret_(secret) {}

  /**
   * @brief Begin accepting connections.
   */
  void run() { do_accept(); }

private:
  tcp::acceptor acceptor_;
  std::string secret_;
  FileSystem fs_;

  void do_accept();
  void on_accept(boost::system::error_code ec, tcp::socket socket);
  void handle_request(HTTP::HTTPREQUEST req, tcp::socket &&socket);
  bool check_auth(const HTTP::HTTPREQUEST &req);
  bool verify_jwt(const std::string &jwt);
};

void RestServer::do_accept() {
  acceptor_.async_accept(
      [this](const boost::system::error_code &ec, tcp::socket socket) {
        on_accept(ec, std::move(socket));
      });
}

void RestServer::on_accept(boost::system::error_code ec, tcp::socket socket) {
  if (!ec) {
    boost::asio::dispatch(
        acceptor_.get_executor(), [this, s = std::move(socket)]() mutable {
          std::string header;
          boost::asio::read_until(s, boost::asio::dynamic_buffer(header),
                                  "\r\n\r\n");
          std::size_t pos = header.find("\r\n\r\n");
          std::string body_prefix;
          if (pos != std::string::npos) {
            body_prefix = header.substr(pos + 4);
            header.erase(pos + 4);
          }
          auto temp_req = HTTP::ParseHttpRequest(header);
          std::size_t remaining = 0;
          auto it = temp_req.headers.find("Content-Length");
          if (it != temp_req.headers.end()) {
            remaining = std::stoul(it->second);
          }
          std::string body = std::move(body_prefix);
          if (body.size() < remaining) {
            std::string rest(remaining - body.size(), '\0');
            boost::asio::read(s, boost::asio::buffer(rest));
            body += rest;
          }
          HTTP::HTTPREQUEST req = HTTP::ParseHttpRequest(header + body);
          handle_request(req, std::move(s));
        });
  }
  do_accept();
}

bool RestServer::check_auth(const HTTP::HTTPREQUEST &req) {
  auto it = req.headers.find("Authorization");
  if (it == req.headers.end())
    return false;
  const std::string &auth = it->second;
  if (auth.empty())
    return false;
  const std::string prefix = "Bearer ";
  if (auth.substr(0, prefix.size()) != prefix)
    return false;
  return verify_jwt(std::string(auth.substr(prefix.size())));
}

bool RestServer::verify_jwt(const std::string &jwt) {
  auto firstDot = jwt.find('.');
  auto secondDot = jwt.find('.', firstDot + 1);
  if (firstDot == std::string::npos || secondDot == std::string::npos)
    return false;
  std::string header = jwt.substr(0, firstDot);
  std::string payload = jwt.substr(firstDot + 1, secondDot - firstDot - 1);
  std::string sig = jwt.substr(secondDot + 1);

  std::string signing_input = header + "." + payload;
  std::vector<unsigned char> mac(crypto_auth_hmacsha256_BYTES);
  crypto_auth_hmacsha256_state state;
  crypto_auth_hmacsha256_init(
      &state, reinterpret_cast<const unsigned char *>(secret_.data()),
      secret_.size());
  crypto_auth_hmacsha256_update(
      &state, reinterpret_cast<const unsigned char *>(signing_input.data()),
      signing_input.size());
  crypto_auth_hmacsha256_final(&state, mac.data());

  std::string decoded = base64::decode<std::string>(sig);
  if (decoded.size() != mac.size())
    return false;
  return std::equal(mac.begin(), mac.end(),
                    reinterpret_cast<const unsigned char *>(decoded.data()));
}

void RestServer::handle_request(HTTP::HTTPREQUEST req, tcp::socket &&socket) {
  HTTP::HTTPRESPONSE res;
  res.protocol = "HTTP/1.1";
  res.statusCodeNumber = 200;
  res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);
  if (!check_auth(req)) {
    res.statusCodeNumber = 401;
    res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);
    res.body = "Unauthorized";
  } else if (req.method == HTTP::HttpMethod::GET &&
             req.uri.rfind("/file/", 0) == 0) {
    std::string path(req.uri.substr(6));
    res.body = fs_.readFile(path);
  } else if (req.method == HTTP::HttpMethod::POST &&
             req.uri.rfind("/file/", 0) == 0) {
    std::string path(req.uri.substr(6));
    fs_.writeFile(path, req.body);
    res.body = "written";
  } else if (req.method == HTTP::HttpMethod::DELETE &&
             req.uri.rfind("/file/", 0) == 0) {
    std::string path(req.uri.substr(6));
    fs_.deleteFile(path);
    res.body = "deleted";
  } else if (req.method == HTTP::HttpMethod::POST &&
             req.uri.rfind("/snapshot/", 0) == 0) {
    std::string rest(req.uri.substr(10));
    if (rest.rfind("/checkout", 0) == 0) {
      std::string name = rest.substr(9);
      bool ok = fs_.snapshotCheckout(name);
      res.body = ok ? "checked" : "error";
    } else {
      fs_.snapshotCreate(rest);
      res.body = "created";
    }
  } else if (req.method == HTTP::HttpMethod::GET && req.uri == "/snapshot") {
    auto snaps = fs_.snapshotList();
    JsonValue arr(JsonValueType::Array);
    for (const auto &s : snaps) {
      JsonValue v(JsonValueType::String);
      v.string_value = s;
      arr.array_value.push_back(v);
    }
    res.body = arr.ToString();
  } else if (req.method == HTTP::HttpMethod::GET &&
             req.uri.rfind("/snapshot/", 0) == 0 && req.uri.size() > 5 &&
             req.uri.compare(req.uri.size() - 5, 5, "/diff") == 0) {
    std::string name = req.uri.substr(10);
    name.erase(name.size() - 5);
    auto diff = fs_.snapshotDiff(name);
    JsonValue arr(JsonValueType::Array);
    for (const auto &d : diff) {
      JsonValue v(JsonValueType::String);
      v.string_value = d;
      arr.array_value.push_back(v);
    }
    res.body = arr.ToString();
  } else {
    res.statusCodeNumber = 404;
    res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);
    res.body = "Not found";
  }
  res.contentType = "application/json";
  std::ostringstream out;
  out << res.protocol << ' ' << res.statusCodeNumber << ' ' << res.reasonPhrase
      << "\r\n";
  out << "Content-Type: " << res.contentType << "\r\n";
  out << "Content-Length: " << res.body.size() << "\r\n\r\n";
  out << res.body;
  boost::asio::write(socket, boost::asio::buffer(out.str()));
  socket.shutdown(tcp::socket::shutdown_send);
}

#ifndef REST_SERVER_DISABLE_MAIN
int main(int argc, char **argv) {
  if (sodium_init() < 0)
    return 1;
  unsigned short port = 8080;
  std::string secret = "secret";
  if (argc > 1)
    port = static_cast<unsigned short>(std::stoi(argv[1]));
  if (argc > 2)
    secret = argv[2];
  boost::asio::io_context ioc{1};
  RestServer server{ioc, port, secret};
  server.run();
  ioc.run();
  return 0;
}
#endif
