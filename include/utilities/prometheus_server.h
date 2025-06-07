#pragma once
#include <thread>

/**
 * @brief Lightweight HTTP server exposing Prometheus metrics.
 */
class PrometheusServer {
public:
    /** Start the metrics server on the given port. */
    static void start(int port = 9100);
private:
    static void run(int port);
};
