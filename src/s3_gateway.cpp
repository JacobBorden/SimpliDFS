#include "s3_gateway.h"
#include "utilities/logger.h"

S3Gateway::S3Gateway(FileSystem &fs) : fs_(fs) {}

void S3Gateway::start(int port) {
    server_ = std::make_unique<httplib::Server>();
    server_->Put(R"(/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.matches[1].str() + "/" + req.matches[2].str();
        if (!fs_.fileExists(path)) {
            fs_.createFile(path);
        }
        fs_.writeFile(path, req.body);
        res.status = 200;
    });
    server_->Get(R"(/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.matches[1].str() + "/" + req.matches[2].str();
        if (!fs_.fileExists(path)) {
            res.status = 404;
            return;
        }
        std::string data = fs_.readFile(path);
        res.set_content(data, "application/octet-stream");
        res.status = 200;
    });
    server_thread_ = std::thread([this, port]() {
        Logger::getInstance().log(LogLevel::INFO, "S3 gateway listening on port " + std::to_string(port));
        server_->listen("0.0.0.0", port);
    });
}

void S3Gateway::stop() {
    if (server_) {
        server_->stop();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

