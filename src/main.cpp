#include <iostream>
#include <string> // Required for std::string
#include <stdexcept> // Required for std::exception (though iostream might bring it)
#include "logger.h" // Include the Logger header

int main() {
    try {
        // Initialize the logger
        // Using "application.log" in the current directory as specified.
        // Defaulting to INFO level for general application messages.
        Logger::init("application.log", LogLevel::INFO);

        Logger::getInstance().log(LogLevel::INFO, "SimpliDFS application started.");
        std::cout << "SimpliDFS is running..." << std::endl;
        // Placeholder for more application logic
        Logger::getInstance().log(LogLevel::INFO, "SimpliDFS application finished successfully.");

    } catch (const std::exception& e) {
        // Log unhandled exceptions
        // Check if logger was initialized, though in this structure it should be.
        // If init itself failed, this log call might also fail or not work as expected.
        // A more robust pre-init logging might use std::cerr directly for init failures.
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled exception: " + std::string(e.what()));
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1; // Indicate an error
    } catch (...) {
        // Catch-all for any other type of exception
        Logger::getInstance().log(LogLevel::FATAL, "Unhandled non-standard exception caught.");
        std::cerr << "FATAL ERROR: Unhandled non-standard exception caught." << std::endl;
        return 1; // Indicate an error
    }
    return 0;
}
