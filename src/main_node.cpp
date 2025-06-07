#include "node/node.h"
#include "utilities/logger.h"
#include <string>
#include <iostream>
#include <stdexcept>
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::seconds

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <NodeName> <Port> <MetaserverAddress> <MetaserverPort> [--cert CERT] [--key KEY] [--ca CA]" << std::endl;
        return 1;
    }

    std::string nodeName = argv[1];
    int port = 0;
    try {
        port = std::stoi(argv[2]);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Invalid port number: " << argv[2] << ". " << ia.what() << std::endl;
        return 1;
    } catch (const std::out_of_range& oor) {
        std::cerr << "Port number out of range: " << argv[2] << ". " << oor.what() << std::endl;
        return 1;
    }

    try {
        Logger::init("node_" + nodeName + ".log", LogLevel::INFO);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Logger initialization failed for node " << nodeName << ": " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " starting on port " + std::to_string(port));

    std::string metaserverAddress = argv[3];
    int metaserverPort = 0;
    try {
        metaserverPort = std::stoi(argv[4]);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Invalid metaserver port number: " << argv[4] << ". " << ia.what() << std::endl;
        return 1;
    } catch (const std::out_of_range& oor) {
        std::cerr << "Metaserver port number out of range: " << argv[4] << ". " << oor.what() << std::endl;
        return 1;
    }

    std::string certFile;
    std::string keyFile;
    std::string caFile;
    for (int i = 5; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cert" && i + 1 < argc) {
            certFile = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            keyFile = argv[++i];
        } else if (arg == "--ca" && i + 1 < argc) {
            caFile = argv[++i];
        }
    }

    try {
        Node node(nodeName, port);
        Logger::getInstance().log(LogLevel::INFO, "Node object '" + nodeName + "' created.");

        if (!certFile.empty() && !keyFile.empty()) {
            if (!node.enableServerTLS(certFile, keyFile)) {
                std::cerr << "FATAL: Failed to enable TLS" << std::endl;
                return 1;
            }
        }

        node.start();
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " server started.");

        // Register with the MetadataManager using provided address and port
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " registering with MetadataManager at " +
                                      metaserverAddress + ":" + std::to_string(metaserverPort) + ".");
        node.registerWithMetadataManager(metaserverAddress, metaserverPort);
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " registration attempt completed.");

        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " running. Main thread entering idle loop.");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            Logger::getInstance().log(LogLevel::DEBUG, "Node " + nodeName + " main thread periodic wake up.");
        }

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in node " + nodeName + ": " + std::string(e.what()));
        return 1;
    } catch (...) {
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled non-standard exception in node " + nodeName + ".");
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " shutting down (unexpectedly reached end of main).");
    return 0;
}
