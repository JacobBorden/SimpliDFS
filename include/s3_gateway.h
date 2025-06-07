#pragma once

#include "utilities/filesystem.h"
#include "httplib.h"
#include <thread>

/**
 * @brief Minimal S3-compatible gateway for SimpliDFS.
 *
 * This class exposes a subset of the S3 REST API allowing clients
 * such as the AWS CLI to upload and download objects using
 * `aws s3 cp` with the `--endpoint-url` flag and `--no-sign-request`.
 */
class S3Gateway {
public:
    /**
     * @brief Construct a gateway using the provided file system.
     * @param fs Reference to the SimpliDFS FileSystem instance.
     */
    explicit S3Gateway(FileSystem &fs);

    /**
     * @brief Start the HTTP server on the given port.
     *
     * This spawns a background thread and returns immediately.
     * @param port Listening port.
     */
    void start(int port);

    /**
     * @brief Stop the HTTP server and join the background thread.
     */
    void stop();

private:
    FileSystem &fs_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
};

