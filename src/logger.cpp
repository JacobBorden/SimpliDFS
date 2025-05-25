#include "logger.h"
#include <cstdio> // For std::rename and std::remove
// #include <vector> // No longer seems necessary

// Initialize static members
std::string Logger::s_filePath = "default_application.log";
LogLevel Logger::s_initialLevel = LogLevel::INFO;
long long Logger::s_maxSize = 10 * 1024 * 1024; // 10MB
int Logger::s_backups = 5;
bool Logger::s_isInitialized = false;

void Logger::init(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal) {
    // According to the plan, allow re-init for now, but it might be better to prevent it or handle it more carefully.
    // if (s_isInitialized) {
    //     std::cerr << "Logger already initialized. Call to init ignored or handled." << std::endl;
    //     return;
    // }
    s_filePath = logFile;
    s_initialLevel = level;
    s_maxSize = maxFileSizeVal;
    s_backups = maxBackupFilesVal;
    s_isInitialized = true;
}

Logger& Logger::getInstance() {
    if (!s_isInitialized) {
        throw std::runtime_error("Logger::getInstance() called before Logger::init(). Please call Logger::init() at application startup.");
    }
    static Logger instance(s_filePath, s_initialLevel, s_maxSize, s_backups);
    return instance;
}

// Private Constructor
Logger::Logger(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal)
    : currentLogLevel(level), logFilePath(logFile), maxFileSize(maxFileSizeVal), maxBackupFiles(maxBackupFilesVal) {
    logFileStream.open(logFilePath, std::ios::app);
    if (!logFileStream.is_open()) {
        std::cerr << "Error: Could not open log file: " << logFilePath << std::endl;
        // Optionally, throw std::runtime_error("Failed to open log file.");
    }
}

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

    if (logFileStream.is_open() && maxFileSize > 0 && logFileStream.tellp() >= maxFileSize) {
        logFileStream.close();

        if (maxBackupFiles == 0) {
            std::remove(logFilePath.c_str());
        } else {
            std::string tooOldPath = logFilePath + "." + std::to_string(maxBackupFiles + 1);
            std::remove(tooOldPath.c_str());

            for (int i = maxBackupFiles; i >= 1; --i) {
                std::string oldPath = logFilePath + "." + std::to_string(i);
                std::string newPath = logFilePath + "." + std::to_string(i + 1);
                std::ifstream oldFileTest(oldPath.c_str());
                if (oldFileTest.good()) {
                    oldFileTest.close();
                    std::rename(oldPath.c_str(), newPath.c_str());
                } else {
                    oldFileTest.close();
                }
            }
            std::rename(logFilePath.c_str(), (logFilePath + ".1").c_str());
        }
        logFileStream.open(logFilePath, std::ios::app);
        if (!logFileStream.is_open()) {
             std::cerr << "Error: Could not re-open log file after rotation: " << logFilePath << std::endl;
        }
    }

    if (logFileStream.is_open()) {
        logFileStream << "{\"timestamp\": \"" << getTimestamp()
                      << "\", \"level\": \"" << levelToString(level) // Correctly calls member function
                      << "\", \"message\": \"" << message << "\"}" << std::endl;
    }
}

void Logger::logToConsole(LogLevel level, const std::string& message){
    if (level < currentLogLevel) {
        return;
    }
	std::cout << "{\"timestamp\": \"" << getTimestamp()
              << "\", \"level\": \"" << levelToString(level) // Correctly calls member function
              << "\", \"message\": \"" << message << "\"}" << std::endl;
}

std::string Logger::getTimestamp(){
	std::time_t currentTime = std::time(nullptr);
	std::tm* localTime = std::localtime(&currentTime);
	char timestamp[20];
	std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",localTime);
	return std::string(timestamp);

}
