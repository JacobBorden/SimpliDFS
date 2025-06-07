#include "utilities/ipfs_gateway.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <thread>
#include <sstream>
#include <iostream>

namespace {
std::string base64url_encode(const unsigned char* data, size_t len) {
    std::string out((len + 2) / 3 * 4, '\0');
    int out_len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&out[0]), data, len);
    out.resize(out_len);
    for (char& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!out.empty() && out.back() == '=') out.pop_back();
    return out;
}

bool verify_jwt(const std::string& token, const std::string& secret) {
    size_t first = token.find('.');
    size_t second = token.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;
    std::string headerPayload = token.substr(0, second);
    std::string signature = token.substr(second + 1);

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), secret.data(), secret.size(),
         reinterpret_cast<const unsigned char*>(headerPayload.data()), headerPayload.size(),
         digest, &digest_len);
    std::string expected = base64url_encode(digest, digest_len);
    return expected == signature;
}
}

void IpfsGateway::start(ChunkStore& store, const std::string& secret, int port) {
    std::thread(&IpfsGateway::run, std::ref(store), secret, port).detach();
}

void IpfsGateway::run(ChunkStore& store, std::string secret, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(sock); return; }
    if (listen(sock, 5) < 0) { close(sock); return; }
    while (true) {
        int c = accept(sock, nullptr, nullptr);
        if (c < 0) continue;
        char buf[4096];
        ssize_t n = read(c, buf, sizeof(buf) - 1);
        if (n <= 0) { close(c); continue; }
        buf[n] = '\0';
        std::string req(buf, n);
        std::istringstream iss(req);
        std::string method, path, version;
        iss >> method >> path >> version;
        std::string line, token;
        std::getline(iss, line); // consume remainder of first line
        while (std::getline(iss, line) && line != "\r") {
            if (line.rfind("Authorization:", 0) == 0) {
                size_t pos = line.find("Bearer ");
                if (pos != std::string::npos) {
                    token = line.substr(pos + 7);
                    if (!token.empty() && token.back() == '\r') token.pop_back();
                }
            }
        }
        if (!verify_jwt(token, secret)) {
            std::string resp = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n";
            send(c, resp.c_str(), resp.size(), 0);
            close(c);
            continue;
        }
        if (method != "GET" || path.rfind("/ipfs/", 0) != 0) {
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(c, resp.c_str(), resp.size(), 0);
            close(c);
            continue;
        }
        std::string cid = path.substr(6);
        if (!store.hasChunk(cid)) {
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(c, resp.c_str(), resp.size(), 0);
            close(c);
            continue;
        }
        auto data = store.getChunk(cid);
        std::string body(reinterpret_cast<const char*>(data.data()), data.size());
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n\r\n" + body;
        send(c, resp.c_str(), resp.size(), 0);
        close(c);
    }
}
