#include "logger.h"
#include <cstdio> // For std::rename and std::remove
// #include <vector> // No longer seems necessary

#include "logger.h"
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
        throw std::runtime_error("Logger not initialized. Call Logger::init() first.");
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
