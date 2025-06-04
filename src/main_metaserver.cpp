// Main entry point for the Metaserver executable

#include <iostream> // For std::cerr, std::to_string
#include <thread>   // For std::thread
#include "utilities/server.h" // For Networking::Server, Networking::ClientConnection
#include "utilities/networkexception.h" // For Networking::NetworkException
#include "utilities/logger.h" // For Logger
#include "metaserver/metaserver.h" // For MetadataManager (declaration)

#include <cstdlib> // For std::atoi
#include <signal.h> // For signal(), SIGPIPE, SIG_IGN

#include <atomic>   // For std::atomic_bool
#include <chrono>   // For std::chrono::seconds
#include <condition_variable> // For std::condition_variable
#include <mutex>    // For std::mutex (used with condition_variable)


// Declare global instances that will be defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// This allows main() to use them.
// extern Networking::Server server; // REMOVED
extern MetadataManager metadataManager;

std::atomic<bool> g_server_running(true);
std::condition_variable g_shutdown_cv;
std::mutex g_shutdown_mutex; // Mutex for the condition variable
const int SAVE_INTERVAL_SECONDS = 5; // Define save interval

// Declare HandleClientConnection which is defined in SimpliDFS_MetaServerLib (metaserver.cpp)
// Alternatively, this declaration could be in a header file (e.g., metaserver.h if it's a free function related to the metaserver operations)
void HandleClientConnection(Networking::Server& server_instance, Networking::ClientConnection _pClient);

void persistence_thread_function() {
    Logger::getInstance().log(LogLevel::INFO, "[PersistenceThread] Background persistence thread started. Save interval: " + std::to_string(SAVE_INTERVAL_SECONDS) + "s.");
    while (g_server_running.load()) {
        // Wait for the interval or a shutdown signal
        std::unique_lock<std::mutex> lock(g_shutdown_mutex);
        if (g_shutdown_cv.wait_for(lock, std::chrono::seconds(SAVE_INTERVAL_SECONDS), []{ return !g_server_running.load(); })) {
            // Awakened by shutdown signal or spurious wake while not running
            if (!g_server_running.load()) {
                Logger::getInstance().log(LogLevel::INFO, "[PersistenceThread] Shutdown signal received, exiting loop.");
                break;
            }
        }
        // lock is released after wait_for returns if not due to predicate, or if predicate true.
        // Re-acquire if necessary, or operate if lock still held and predicate was false (timeout path)

        // Timeout occurred, proceed with periodic save if server is still running
        if (!g_server_running.load()) break; // Check again after timeout

        if (metadataManager.isDirty()) {
            Logger::getInstance().log(LogLevel::INFO, "[PersistenceThread] Metadata is dirty, attempting to save.");
            try {
                metadataManager.saveMetadata("file_metadata.dat", "node_registry.dat");
                metadataManager.clearDirty(); // Clear dirty flag only after successful save
                Logger::getInstance().log(LogLevel::INFO, "[PersistenceThread] Metadata successfully saved.");
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::ERROR, "[PersistenceThread] Exception during saveMetadata: " + std::string(e.what()));
                // Decide if to retry, or just wait for next interval. For now, wait.
            }
        } else {
            Logger::getInstance().log(LogLevel::DEBUG, "[PersistenceThread] Metadata not dirty, skipping save.");
        }
    }
    Logger::getInstance().log(LogLevel::INFO, "[PersistenceThread] Background persistence thread finishing.");
    // Perform one final save on shutdown if dirty, if desired and safe
    // This might be better handled in main's shutdown sequence to ensure server loop is not running.
}

int main(int argc, char* argv[])
{
    // Ignore SIGPIPE: prevents termination if writing to a closed socket
    signal(SIGPIPE, SIG_IGN);

    int port = 50505; // Default port
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port == 0) { // Basic error check for atoi
            std::cerr << "FATAL: Invalid port number provided: " << argv[1] << std::endl;
            return 1;
        }
    }

    try {
        Logger::init(Logger::CONSOLE_ONLY_OUTPUT, LogLevel::DEBUG); // Attempt to use console output constant
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Logger initialization failed for metaserver: " << e.what() << std::endl;
        return 1;
    }

    Logger::getInstance().log(LogLevel::INFO, "Metaserver starting up...");
    // Assuming loadMetadata is a public method of MetadataManager
    // and metadataManager instance is accessible (declared extern above).
    Logger::getInstance().log(LogLevel::INFO, "Loading metadata from file_metadata.dat and node_registry.dat");
    metadataManager.loadMetadata("file_metadata.dat", "node_registry.dat");

    Logger::getInstance().log(LogLevel::INFO, "Main: Starting persistence thread.");
    std::thread persist_thread(persistence_thread_function);

    Networking::Server local_server(port); // Create server instance with parsed port

    // Attempt to start the server
    if (!local_server.startListening()) {
        std::cerr << "DIAGNOSTIC: startListening() returned false. Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (startListening returned false). Port: " + std::to_string(local_server.GetPort()));
        return 1; // Exit if server cannot start
    }
    std::cerr << "DIAGNOSTIC: Returned from startListening() call. ServerIsRunning: " << (local_server.ServerIsRunning() ? "true" : "false") << std::endl;

    // local_server.ServerIsRunning() should now be true if startListening succeeded
    if (local_server.ServerIsRunning())
    {
        std::cerr << "DIAGNOSTIC: Entering while(true) accept loop." << std::endl;
        Logger::getInstance().log(LogLevel::INFO, "Metaserver is running and listening on port " + std::to_string(local_server.GetPort()));
        while (true)
        {
            std::cerr << "DIAGNOSTIC: Top of while(true) accept loop." << std::endl;
            try {
                Networking::ClientConnection client = local_server.Accept();
                // Assuming client.clientSocket is accessible and is the raw socket descriptor.
                // On POSIX, an invalid socket descriptor is often < 0.
                // This is a placeholder check; a proper check might involve client.isValid() or similar.
                if (client.clientSocket < 0) { // Placeholder for INVALIDSOCKET(client.clientSocket)
                     std::cerr << "DIAGNOSTIC: Main loop: local_server.Accept() returned an invalid client socket value: " << client.clientSocket << ". Will attempt to create thread anyway to observe behavior." << std::endl;
                }
                Logger::getInstance().log(LogLevel::INFO, "Accepted new client connection from " + local_server.GetClientIPAddress(client));
                std::cerr << "DIAGNOSTIC: Client accepted, about to create and detach thread." << std::endl;
                std::thread clientThread(HandleClientConnection, std::ref(local_server), client);
                clientThread.detach();
                Logger::getInstance().log(LogLevel::DEBUG, "Detached thread to handle client " + local_server.GetClientIPAddress(client));
            } catch (const Networking::NetworkException& ne) {
                Logger::getInstance().log(LogLevel::ERROR, "Network exception in main server loop: " + std::string(ne.what()));
                std::cerr << "DIAGNOSTIC: NetworkException in main loop: " << ne.what() << std::endl; // Also print to cerr
            } catch (const std::exception& e) {
                Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception in main server loop: " + std::string(e.what()));
                std::cerr << "DIAGNOSTIC: std::exception in main loop: " << e.what() << std::endl; // Also print to cerr
                break; // Exit on fatal error
            } catch (...) {
                Logger::getInstance().log(LogLevel::FATAL, "Unknown unhandled exception in main server loop.");
                std::cerr << "DIAGNOSTIC: Unknown exception in main loop." << std::endl; // Also print to cerr
                break; // Exit on fatal error
            }
        }
    } else {
        std::cerr << "DIAGNOSTIC: ServerIsRunning() is false after startListening() call. Exiting." << std::endl;
        Logger::getInstance().log(LogLevel::FATAL, "Metaserver failed to start listening (ServerIsRunning is false).");
    }

    Logger::getInstance().log(LogLevel::INFO, "Main: Signaling persistence thread to shut down.");
    {
        std::lock_guard<std::mutex> lock(g_shutdown_mutex);
        g_server_running = false;
    }
    g_shutdown_cv.notify_one(); // Notify the persistence thread

    if (persist_thread.joinable()) {
        Logger::getInstance().log(LogLevel::INFO, "Main: Joining persistence thread.");
        persist_thread.join();
        Logger::getInstance().log(LogLevel::INFO, "Main: Persistence thread joined.");
    }

    // Perform a final save if metadata is still dirty
    // This ensures data is saved if changes occurred right before shutdown signal
    // and the persistence thread didn't get to save it.
    if (metadataManager.isDirty()) {
        Logger::getInstance().log(LogLevel::INFO, "Main: Performing final metadata save on shutdown.");
        try {
            metadataManager.saveMetadata("file_metadata.dat", "node_registry.dat");
            metadataManager.clearDirty();
            Logger::getInstance().log(LogLevel::INFO, "Main: Final metadata save successful.");
        } catch (const std::exception& e) {
            Logger::getInstance().log(LogLevel::ERROR, "Main: Exception during final metadata save: " + std::string(e.what()));
        }
    }

    Logger::getInstance().log(LogLevel::INFO, "Metaserver shutting down completely.");
    return 0;
}
