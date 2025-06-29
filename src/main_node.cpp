#include "node/node.h"
#include "utilities/fips.h"
#include "utilities/key_manager.hpp"
#include "utilities/logger.h"
#include <chrono> // For std::chrono::seconds
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread> // For std::this_thread::sleep_for
#include <yaml-cpp/yaml.h>

struct RuntimeOptions {
  int compressionLevel = 1;
  std::string cipherAlgorithm = "XChaCha20-Poly1305";
};

static RuntimeOptions loadRuntimeOptions() {
  RuntimeOptions opts;
  const char *cfg = std::getenv("SIMPLIDFS_CONFIG");
  if (!cfg)
    cfg = "simplidfs_config.yaml";
  try {
    YAML::Node node = YAML::LoadFile(cfg);
    if (node["compression_level"])
      opts.compressionLevel = node["compression_level"].as<int>();
    if (node["cipher_algorithm"])
      opts.cipherAlgorithm = node["cipher_algorithm"].as<std::string>();
  } catch (...) {
  }
  if (const char *env = std::getenv("SIMPLIDFS_COMPRESSION_LEVEL"))
    opts.compressionLevel = std::atoi(env);
  if (const char *env = std::getenv("SIMPLIDFS_CIPHER_ALGO"))
    opts.cipherAlgorithm = env;
  return opts;
}

int main(int argc, char *argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " <NodeName> <Port> <MetaserverAddress> <MetaserverPort> "
                 "[--cert CERT] [--key KEY] [--ca CA] [--quote QUOTE]"
              << std::endl;
    return 1;
  }

  std::string nodeName = argv[1];
  int port = 0;
  try {
    port = std::stoi(argv[2]);
  } catch (const std::invalid_argument &ia) {
    std::cerr << "Invalid port number: " << argv[2] << ". " << ia.what()
              << std::endl;
    return 1;
  } catch (const std::out_of_range &oor) {
    std::cerr << "Port number out of range: " << argv[2] << ". " << oor.what()
              << std::endl;
    return 1;
  }

  try {
    std::string logDir = "/var/logs/simplidfs";
    try {
      std::filesystem::create_directories(logDir);
    } catch (...) {
    }
    Logger::init(logDir + "/" + nodeName + ".log", LogLevel::INFO);
  } catch (const std::exception &e) {
    std::cerr << "FATAL: Logger initialization failed for node " << nodeName
              << ": " << e.what() << std::endl;
    return 1;
  }

  if (!fips_self_test()) {
    std::cerr << "FATAL: FIPS self test failed" << std::endl;
    return 1;
  }

  try {
    simplidfs::KeyManager::getInstance().initialize();
  } catch (const std::exception &e) {
    std::cerr << "FATAL: KeyManager initialization failed: " << e.what()
              << std::endl;
    return 1;
  }

  Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName +
                                                " starting on port " +
                                                std::to_string(port));

  RuntimeOptions opts = loadRuntimeOptions();
  BlockIO::CipherAlgorithm algo = BlockIO::CipherAlgorithm::XCHACHA20_POLY1305;
  if (opts.cipherAlgorithm == "AES-256-GCM" &&
      crypto_aead_aes256gcm_is_available()) {
    algo = BlockIO::CipherAlgorithm::AES_256_GCM;
  } else if (opts.cipherAlgorithm != "XChaCha20-Poly1305") {
    Logger::getInstance().log(
        LogLevel::WARN, "Unsupported cipher algorithm " + opts.cipherAlgorithm +
                            ", defaulting to XChaCha20-Poly1305");
  }

  std::string metaserverAddress = argv[3];
  int metaserverPort = 0;
  try {
    metaserverPort = std::stoi(argv[4]);
  } catch (const std::invalid_argument &ia) {
    std::cerr << "Invalid metaserver port number: " << argv[4] << ". "
              << ia.what() << std::endl;
    return 1;
  } catch (const std::out_of_range &oor) {
    std::cerr << "Metaserver port number out of range: " << argv[4] << ". "
              << oor.what() << std::endl;
    return 1;
  }

  std::string certFile;
  std::string keyFile;
  std::string caFile;
  std::string quoteFile;
  for (int i = 5; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--cert" && i + 1 < argc) {
      certFile = argv[++i];
    } else if (arg == "--key" && i + 1 < argc) {
      keyFile = argv[++i];
    } else if (arg == "--ca" && i + 1 < argc) {
      caFile = argv[++i];
    } else if (arg == "--quote" && i + 1 < argc) {
      quoteFile = argv[++i];
    }
  }

  try {
    Node node(nodeName, port, opts.compressionLevel, algo);
    Logger::getInstance().log(LogLevel::INFO,
                              "Node object '" + nodeName +
                                  "' created. Compression level " +
                                  std::to_string(opts.compressionLevel) +
                                  ", cipher " + opts.cipherAlgorithm);

    if (!certFile.empty() && !keyFile.empty()) {
      if (!node.enableServerTLS(certFile, keyFile)) {
        std::cerr << "FATAL: Failed to enable TLS" << std::endl;
        return 1;
      }
    }

    node.start(metaserverAddress, metaserverPort);
    Logger::getInstance().log(LogLevel::INFO,
                              "Node " + nodeName + " server started.");

    // Register with the MetadataManager using provided address and port
    Logger::getInstance().log(
        LogLevel::INFO,
        "Node " + nodeName + " registering with MetadataManager at " +
            metaserverAddress + ":" + std::to_string(metaserverPort) + ".");
    node.registerWithMetadataManager(metaserverAddress, metaserverPort,
                                     quoteFile);
    Logger::getInstance().log(LogLevel::INFO,
                              "Node " + nodeName +
                                  " registration attempt completed.");

    Logger::getInstance().log(LogLevel::INFO,
                              "Node " + nodeName +
                                  " running. Main thread entering idle loop.");
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(60));
      Logger::getInstance().log(LogLevel::DEBUG,
                                "Node " + nodeName +
                                    " main thread periodic wake up.");
    }

  } catch (const std::exception &e) {
    Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in node " +
                                                   nodeName + ": " +
                                                   std::string(e.what()));
    return 1;
  } catch (...) {
    Logger::getInstance().log(LogLevel::FATAL,
                              "Unhandled non-standard exception in node " +
                                  nodeName + ".");
    return 1;
  }

  Logger::getInstance().log(
      LogLevel::INFO, "Node " + nodeName +
                          " shutting down (unexpectedly reached end of main).");
  return 0;
}
