#include "node/node.h"
#include "utilities/logger.h"
#include <string>
#include <iostream>
#include <stdexcept>
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::seconds
#include <yaml-cpp/yaml.h>

struct RuntimeOptions {
    int compressionLevel = 1;
    std::string cipherAlgorithm = "AES-256-GCM";
};

static RuntimeOptions loadRuntimeOptions() {
    RuntimeOptions opts;
    const char* cfg = std::getenv("SIMPLIDFS_CONFIG");
    if (!cfg) cfg = "simplidfs_config.yaml";
    try {
        YAML::Node node = YAML::LoadFile(cfg);
        if (node["compression_level"]) opts.compressionLevel = node["compression_level"].as<int>();
        if (node["cipher_algorithm"]) opts.cipherAlgorithm = node["cipher_algorithm"].as<std::string>();
    } catch (...) {
    }
    if (const char* env = std::getenv("SIMPLIDFS_COMPRESSION_LEVEL")) opts.compressionLevel = std::atoi(env);
    if (const char* env = std::getenv("SIMPLIDFS_CIPHER_ALGO")) opts.cipherAlgorithm = env;
    return opts;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <NodeName> <Port> <MetaserverAddress> <MetaserverPort>" << std::endl;
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

    RuntimeOptions opts = loadRuntimeOptions();
    BlockIO::CipherAlgorithm algo = BlockIO::CipherAlgorithm::AES_256_GCM;
    if (opts.cipherAlgorithm != "AES-256-GCM") {
        Logger::getInstance().log(LogLevel::WARN,
            "Unsupported cipher algorithm " + opts.cipherAlgorithm + ", defaulting to AES-256-GCM");
    }

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

    try {
        Node node(nodeName, port, opts.compressionLevel, algo);
        Logger::getInstance().log(LogLevel::INFO,
            "Node object '" + nodeName + "' created. Compression level " +
            std::to_string(opts.compressionLevel) + ", cipher " + opts.cipherAlgorithm);

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
