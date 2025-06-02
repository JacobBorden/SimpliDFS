#include "utilities/logger.h"
#include <cstdio> // For std::rename and std::remove
// #include <vector> // No longer seems necessary

#include "utilities/logger.h"
#include <cstdio> // For std::rename and std::remove
#include <sstream> // For escapeJsonString
#include <string>  // For std::string

// Anonymous namespace for file-static helper functions
namespace {
    std::string escapeJsonString(const std::string& input) {
        std::ostringstream ss;
        for (char c : input) {
            switch (c) {
                case '\\': ss << "\\\\"; break;
                case '"': ss << "\\\""; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                // Skipping / for now as per instruction
                default: ss << c; break;
            }
        }
        return ss.str();
    }
} // anonymous namespace

// Initialize static members
Logger* Logger::s_instance = nullptr;

void Logger::init(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal) {
    delete s_instance; // Safe to delete nullptr
    s_instance = new Logger(logFile, level, maxFileSizeVal, maxBackupFilesVal);
}

Logger& Logger::getInstance() {
    if (!s_instance) {
        std::cerr << "CRITICAL_WARNING: Logger::getInstance() called when s_instance is null. Attempting emergency initialization to 'emergency_default.log'." << std::endl;
        try {
            // Attempt to initialize the logger to an emergency default.
            // Logger::init handles 'new Logger' which can throw std::bad_alloc.
            Logger::init("emergency_default.log", LogLevel::WARN);
            // If Logger::init was successful, s_instance is now (or should be) non-nullptr.
        }
        catch (const std::bad_alloc& e) {
            // This catch is specifically if 'new Logger()' inside Logger::init fails due to memory.
            // s_instance would remain nullptr if it was nullptr before, or become nullptr if init deleted an old one then failed to new.
            std::cerr << "CRITICAL_ERROR: Emergency Logger::init failed during memory allocation: " << e.what() << std::endl;
            // s_instance is confirmed to be nullptr or is effectively nullptr after this.
        }
        catch (const std::exception& e) {
            // Catch any other unexpected std::exception if Logger::init or Logger constructor were to throw more.
            // (Currently, Logger constructor only prints to cerr for file errors, doesn't throw for that).
            std::cerr << "CRITICAL_ERROR: Emergency Logger::init failed with an unexpected standard exception: " << e.what() << std::endl;
        }
        catch (...) {
            // Catch any other non-standard exception.
            std::cerr << "CRITICAL_ERROR: Emergency Logger::init failed with an unknown exception." << std::endl;
        }

        // After attempting emergency initialization, re-check s_instance.
        if (!s_instance) {
            // If s_instance is still null, the emergency initialization failed catastrophically.
            throw std::runtime_error("Logger not initialized. Call Logger::init() first. Emergency init also failed.");
        }
        // If we reach here, s_instance is now valid (pointing to an instance).
        // That instance might have failed to open its log file ("emergency_default.log"), 
        // but the Logger object itself exists, preventing a crash on dereferencing s_instance.
        // The Logger::log method handles cases where its file stream isn't open.
    }
    return *s_instance;
}

// Constructor (was private, now effectively public via static init, but keep private for direct instantiation control)
Logger::Logger(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal)
    : currentLogLevel(level), logFilePath(logFile), maxFileSize(maxFileSizeVal), maxBackupFiles(maxBackupFilesVal) {
    logFileStream.open(logFilePath, std::ios::app);
    if (!logFileStream.is_open()) {
        // Using std::cerr here as logger might not be fully functional for itself yet.
        std::cerr << "Error: Could not open log file: " << logFilePath << std::endl;
    }
}

// Public Destructor
Logger::~Logger(){
	if (logFileStream.is_open()) {
		logFileStream.close();
	}
}

void Logger::setLogLevel(LogLevel level) {
    currentLogLevel = level;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case TRACE: return "TRACE";
        case DEBUG: return "DEBUG";
        case INFO: return "INFO";
        case WARN: return "WARN";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const std::string& message){
    if (level < currentLogLevel) {
        return;
    }

    // Removed local lambda, will use file-static helper function

    if (logFileStream.is_open() && maxFileSize > 0) { // Check maxFileSize > 0
        if (logFileStream.is_open()) logFileStream.clear(); // Clear any error flags
        if (logFileStream.is_open()) logFileStream.flush(); // Flush before tellp
        if (logFileStream.tellp() >= maxFileSize) {
            this->logFileStream.close(); // Close before rename/remove

            if (maxBackupFiles == 0) {
                std::remove(this->logFilePath.c_str());
            } else {
                std::string tooOldPath = this->logFilePath + "." + std::to_string(maxBackupFiles + 1);
                std::remove(tooOldPath.c_str());

                for (int i = maxBackupFiles; i >= 1; --i) {
                    std::string oldPath = this->logFilePath + "." + std::to_string(i);
                    std::string newPath = this->logFilePath + "." + std::to_string(i + 1);
                    std::ifstream oldFileTest(oldPath.c_str());
                    if (oldFileTest.good()) {
                        oldFileTest.close();
                        std::remove(newPath.c_str()); // Ensure target of rename does not exist
                        std::rename(oldPath.c_str(), newPath.c_str());
                    } else {
                        oldFileTest.close();
                    }
                }
                std::rename(this->logFilePath.c_str(), (this->logFilePath + ".1").c_str());
            }
            // Reopen the primary log file path
            this->logFileStream.open(this->logFilePath, std::ios::app);
            if (!this->logFileStream.is_open()) {
                 std::cerr << "Error: Could not re-open log file after rotation: " << this->logFilePath << std::endl;
            }
        }
    }


    if (logFileStream.is_open()) {
        logFileStream << "{\"timestamp\": \"" << getTimestamp()
                      << "\", \"level\": \"" << levelToString(level)
                      << "\", \"message\": \"" << escapeJsonString(message) << "\"}" << std::endl;
    }
}

void Logger::logToConsole(LogLevel level, const std::string& message){
    if (level < currentLogLevel) {
        return;
    }
    // Removed local lambda, will use file-static helper function
	std::cout << "{\"timestamp\": \"" << getTimestamp()
              << "\", \"level\": \"" << levelToString(level)
              << "\", \"message\": \"" << escapeJsonString(message) << "\"}" << std::endl;
}

std::string Logger::getTimestamp(){
	std::time_t currentTime = std::time(nullptr);
	std::tm* localTime = std::localtime(&currentTime);
	char timestamp[20];
	std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",localTime);
	return std::string(timestamp);

}
