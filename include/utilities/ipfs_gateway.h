#pragma once
#include <string>
#include "utilities/chunk_store.hpp"

/**
 * @brief Lightweight HTTP gateway serving chunks from a ChunkStore.
 *
 * The gateway exposes a minimal HTTP interface which mimics a subset
 * of the IPFS gateway functionality. Each request must include a
 * valid JWT via the `Authorization` header.
 */
class IpfsGateway {
public:
    /**
     * @brief Launch the gateway in a detached thread.
     *
     * @param store  Reference to the chunk store providing data.
     * @param secret Shared secret used to verify JWT signatures.
     * @param port   Port number to listen on. Defaults to 8081.
     */
    static void start(ChunkStore& store, const std::string& secret, int port = 8081);
private:
    /**
     * @brief Internal server loop executed on a background thread.
     * @param store  Chunk store to read data from.
     * @param secret Secret used for JWT verification.
     * @param port   Listening port for incoming connections.
     */
    static void run(ChunkStore& store, std::string secret, int port);
};
