#include "utilities/filesystem.h"
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cppcodec/base64_url_unpadded.hpp>
#include <sodium.h>
#include <string>
#include <vector>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
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
  void on_accept(boost::beast::error_code ec, tcp::socket socket);
  void handle_request(http::request<http::string_body> &&req,
                      tcp::socket &&socket);
  bool check_auth(const http::request<http::string_body> &req);
  bool verify_jwt(const std::string &jwt);
};

void RestServer::do_accept() {
  acceptor_.async_accept(
      boost::beast::bind_front_handler(&RestServer::on_accept, this));
}

void RestServer::on_accept(boost::beast::error_code ec, tcp::socket socket) {
  if (!ec) {
    boost::asio::dispatch(acceptor_.get_executor(),
                          [this, s = std::move(socket)]() mutable {
                            boost::beast::flat_buffer buffer;
                            http::request<http::string_body> req;
                            http::read(s, buffer, req);
                            handle_request(std::move(req), std::move(s));
                          });
  }
  do_accept();
}

bool RestServer::check_auth(const http::request<http::string_body> &req) {
  auto auth = req[http::field::authorization];
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

void RestServer::handle_request(http::request<http::string_body> &&req,
                                tcp::socket &&socket) {
  http::response<http::string_body> res{http::status::ok, req.version()};
  if (!check_auth(req)) {
    res.result(http::status::unauthorized);
    res.body() = "Unauthorized";
  } else if (req.method() == http::verb::get &&
             req.target().starts_with("/file/")) {
    std::string path(req.target().substr(6));
    res.body() = fs_.readFile(path);
  } else if (req.method() == http::verb::post &&
             req.target().starts_with("/file/")) {
    std::string path(req.target().substr(6));
    fs_.writeFile(path, req.body());
    res.body() = "written";
  } else if (req.method() == http::verb::delete_ &&
             req.target().starts_with("/file/")) {
    std::string path(req.target().substr(6));
    fs_.deleteFile(path);
    res.body() = "deleted";
  } else if (req.method() == http::verb::post &&
             req.target().starts_with("/snapshot/")) {
    std::string rest(req.target().substr(10));
    if (rest.rfind("/checkout", 0) == 0) {
      std::string name = rest.substr(9);
      bool ok = fs_.snapshotCheckout(name);
      res.body() = ok ? "checked" : "error";
    } else {
      fs_.snapshotCreate(rest);
      res.body() = "created";
    }
  } else if (req.method() == http::verb::get && req.target() == "/snapshot") {
    auto snaps = fs_.snapshotList();
    std::string body = "[";
    for (size_t i = 0; i < snaps.size(); ++i) {
      body += '"' + snaps[i] + '"';
      if (i + 1 < snaps.size())
        body += ',';
    }
    body += ']';
    res.body() = body;
  } else if (req.method() == http::verb::get &&
             req.target().starts_with("/snapshot/") &&
             req.target().ends_with("/diff")) {
    std::string name = req.target().substr(10);
    name.erase(name.size() - 5);
    auto diff = fs_.snapshotDiff(name);
    std::string body = "[";
    for (size_t i = 0; i < diff.size(); ++i) {
      body += '"' + diff[i] + '"';
      if (i + 1 < diff.size())
        body += ',';
    }
    body += ']';
    res.body() = body;
  } else {
    res.result(http::status::not_found);
    res.body() = "Not found";
  }
  res.set(http::field::content_type, "application/json");
  res.content_length(res.body().size());
  http::write(socket, res);
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
