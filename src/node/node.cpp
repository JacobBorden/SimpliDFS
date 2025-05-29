#include "node/node.h"
#include "utilities/logger.h" // Include the Logger header
#include <string>   // For std::string, std::stoi
#include <iostream> // For std::cerr (though logger will replace most)
#include <stdexcept> // For std::exception

// Assuming Node class methods like start() and registerWithMetadataManager()
// would internally use Logger::getInstance() if they need logging.
// This main function acts as the entry point for a node executable.

int main(int argc, char* argv[]) {
    if (argc < 3) {
        // Logger might not be initialized yet, so use std::cerr for this specific early error.
        std::cerr << "Usage: " << argv[0] << " <NodeName> <Port>" << std::endl;
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
        // Initialize logger for this node. Each node will have its own log file.
        Logger::init("node_" + nodeName + ".log", LogLevel::INFO);
    } catch (const std::exception& e) {
        // If logger init fails, this is a critical issue. Output to cerr.
        std::cerr << "FATAL: Logger initialization failed for node " << nodeName << ": " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " starting on port " + std::to_string(port));

    try {
        Node node(nodeName, port); // Assuming Node constructor might log
        Logger::getInstance().log(LogLevel::INFO, "Node object '" + nodeName + "' created.");
        
        node.start(); // Assuming Node::start might log
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " started.");

        // Register with the MetadataManager
        // Replace "127.0.0.1" with actual MetadataManager IP if different
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " registering with MetadataManager at 127.0.0.1:50505.");
        node.registerWithMetadataManager("127.0.0.1", 50505); 
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " registration attempt completed.");

        // Keep the main thread running indefinitely
        Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " running. Main thread entering idle loop.");
        while (true) {
            // Add periodic tasks here if needed, e.g., sending heartbeats
            // For now, just sleep.
            std::this_thread::sleep_for(std::chrono::seconds(60)); // Sleep for a longer duration
            Logger::getInstance().log(LogLevel::DEBUG, "Node " + nodeName + " main thread periodic wake up."); 
        }

    } catch (const std::exception& e) {
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in node " + nodeName + ": " + std::string(e.what()));
        return 1; // Indicate an error
    } catch (...) {
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled non-standard exception in node " + nodeName + ".");
        return 1; // Indicate an error
    }
    
    Logger::getInstance().log(LogLevel::INFO, "Node " + nodeName + " shutting down (unexpectedly reached end of main).");
    return 0;
}
