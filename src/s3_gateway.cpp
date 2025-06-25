#include "s3_gateway.h"
#include "utilities/logger.h"

using tcp = boost::asio::ip::tcp;

S3Gateway::S3Gateway(FileSystem &fs) : fs_(fs) {}

void S3Gateway::start(int port) {
  running_ = true;
  io_ = std::make_unique<boost::asio::io_context>();
  acceptor_ = std::make_unique<tcp::acceptor>(
      *io_, tcp::endpoint(tcp::v4(), static_cast<unsigned short>(port)));
  server_thread_ = std::thread([this, port]() {
    Logger::getInstance().log(LogLevel::INFO, "S3 gateway listening on port" +
                                                  std::to_string(port));
    while (running_) {
      tcp::socket socket(*io_);
      boost::system::error_code ec;
      acceptor_->accept(socket, ec);
      if (ec) {
        continue;
      }
      std::string header;
      boost::asio::read_until(socket, boost::asio::dynamic_buffer(header),
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
      std::string body = body_prefix;
      if (body.size() < remaining) {
        std::string rest(remaining - body.size(), '\0');
        boost::asio::read(socket, boost::asio::buffer(rest));
        body += rest;
      }
      HTTP::HTTPREQUEST req = HTTP::ParseHttpRequest(header + body);

      HTTP::HTTPRESPONSE res;
      res.protocol = "HTTP/1.1";
      res.statusCodeNumber = 200;
      res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);

      if (req.method == HTTP::HttpMethod::PUT) {
        std::string path = req.uri.substr(1);
        if (!fs_.fileExists(path)) {
          fs_.createFile(path);
        }
        fs_.writeFile(path, req.body);
      } else if (req.method == HTTP::HttpMethod::GET) {
        std::string path = req.uri.substr(1);
        if (!fs_.fileExists(path)) {
          res.statusCodeNumber = 404;
          res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);
        } else {
          std::string data = fs_.readFile(path);
          res.body = data;
          res.contentType = "application/octet-stream";
        }
      } else {
        res.statusCodeNumber = 405;
        res.reasonPhrase = HTTP::statusCode.at(res.statusCodeNumber);
      }

      if (res.contentType.empty()) {
        res.contentType = "text/plain";
      }
      std::ostringstream out;
      out << res.protocol << ' ' << res.statusCodeNumber << ' '
          << res.reasonPhrase << "\r\n";
      out << "Content-Type: " << res.contentType << "\r\n";
      out << "Content-Length: " << res.body.size() << "\r\n\r\n";
      out << res.body;
      boost::asio::write(socket, boost::asio::buffer(out.str()));
      boost::system::error_code ignored;
      socket.shutdown(tcp::socket::shutdown_both, ignored);
      socket.close(ignored);
    }
  });
}

void S3Gateway::stop() {
  running_ = false;
  if (acceptor_) {
    boost::system::error_code ec;
    acceptor_->close(ec);
  }
  if (io_) {
    io_->stop();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}
