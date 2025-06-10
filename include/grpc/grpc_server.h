#pragma once
#include <string>

class FileSystem;

/**
 * @brief Start a gRPC server exposing basic filesystem operations.
 *
 * The server listens on @p address and delegates all requests to the
 * provided @p FileSystem instance.
 *
 * @param address Address in the form "host:port" to bind the server.
 * @param fs      FileSystem used to service RPCs.
 */
void RunGrpcServer(const std::string& address, FileSystem& fs);
