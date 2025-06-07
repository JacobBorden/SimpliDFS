#pragma once
#include <string>
#include "utilities/chunk_store.hpp"

/**
 * @brief Lightweight HTTP gateway serving chunks by CID.
 *
 * Requests must include a valid JWT in the Authorization header.
 */
class IpfsGateway {
public:
    /** Start the gateway on the given port using the shared secret. */
    static void start(ChunkStore& store, const std::string& secret, int port = 8081);
private:
    static void run(ChunkStore& store, std::string secret, int port);
};
