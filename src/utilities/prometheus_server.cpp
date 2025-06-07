#include "utilities/prometheus_server.h"
#include "utilities/metrics.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <iostream>

void PrometheusServer::start(int port) {
    std::thread(run, port).detach();
}

void PrometheusServer::run(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) return;
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if(bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { close(sock); return; }
    if(listen(sock, 5) < 0) { close(sock); return; }
    while(true) {
        int c = accept(sock, nullptr, nullptr);
        if(c < 0) continue;
        char buf[1024];
        read(c, buf, sizeof(buf));
        std::string body = MetricsRegistry::instance().toPrometheus();
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        send(c, resp.c_str(), resp.size(), 0);
        close(c);
    }
}
