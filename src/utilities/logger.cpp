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

#include <new> // For std::bad_alloc

#include <new> // For std::bad_alloc
#include <mutex> // For std::lock_guard, though already in logger.h

// Initialize static members
Logger* Logger::s_instance = nullptr;
std::mutex Logger::s_mutex;
std::once_flag Logger::s_once_flag; // Initialize static once_flag
const std::string Logger::CONSOLE_ONLY_OUTPUT = "::CONSOLE::";

void Logger::init(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal) {
    std::lock_guard<std::mutex> lock(s_mutex);
    // Removed verbose entry log: std::cerr << "[Logger::init] CALLED for file: " << logFile << ". Current s_instance: " << s_instance << std::endl;

    // Removed verbose pre-deletion log:
    // if (s_instance) {
    //     std::cerr << "[Logger::init] Deleting existing s_instance logging to: " << s_instance->logFilePath << std::endl;
    // }
    
    delete s_instance; // Safe to delete nullptr
    s_instance = nullptr; // Explicitly nullify before attempting new allocation.
    // Removed verbose nullification log: std::cerr << "[Logger::init] s_instance explicitly set to nullptr." << std::endl;

    try {
        s_instance = new Logger(logFile, level, maxFileSizeVal, maxBackupFilesVal);
        // Removed verbose success log:
        // if (s_instance) { 
        //      std::cerr << "[Logger::init] new Logger SUCCEEDED. s_instance: " << s_instance 
        //                << ", logging to: " << s_instance->logFilePath 
        //                << ", is_open: " << s_instance->logFileStream.is_open() << std::endl;
        // } else {
        //      std::cerr << "[Logger::init] CRITICAL: new Logger returned nullptr but did not throw! s_instance REMAINS nullptr." << std::endl;
        // }
    } catch (const std::bad_alloc& bae) {
        // s_instance remains nullptr because it was set to nullptr before the try block.
        std::cerr << "[Logger::init] CRITICAL: new Logger FAILED due to std::bad_alloc: " << bae.what() << ". s_instance REMAINS nullptr." << std::endl;
    } catch (const std::exception& e) {
        // s_instance remains nullptr.
        std::cerr << "[Logger::init] CRITICAL: new Logger FAILED due to std::exception: " << e.what() << ". s_instance REMAINS nullptr." << std::endl;
    } catch (...) {
        // s_instance remains nullptr.
        std::cerr << "[Logger::init] CRITICAL: new Logger FAILED due to unknown exception. s_instance REMAINS nullptr." << std::endl;
    }
    // Removed verbose completion log: std::cerr << "[Logger::init] COMPLETED. Final s_instance: " << s_instance << std::endl;
}

Logger& Logger::getInstance() {
    // Double-checked locking pattern can be considered here for performance if getInstance is called extremely frequently
    // and locking is a bottleneck. However, for typical logger usage, a simple lock is often sufficient and safer to implement.
    // The emergency init path complicates DCLP.
    std::call_once(s_once_flag, []() {
        // This lambda will be executed only once.
        // It ensures a default instance is created if no instance exists when getInstance is first called.
        // This typically happens if getInstance() is called during static initialization, before main()'s Logger::init().
        if (!s_instance) { // s_instance should be null here unless init() was called by another thread before this call_once (unlikely for static init)
            std::cerr << "Logger::getInstance() called before explicit Logger::init(). Creating default console logger." << std::endl;
            // Create a default logger that logs to console.
            // Parameters for Logger constructor: (logFile, level, maxFileSizeVal, maxBackupFilesVal)
            s_instance = new Logger(Logger::CONSOLE_ONLY_OUTPUT, LogLevel::WARN, 0, 0);
            // Note: No mutex needed for s_instance assignment here as call_once guarantees serial execution.
        }
    });

    // After call_once, s_instance should be non-null (either default or from previous init).
    // However, Logger::init() can delete and replace s_instance.
    // So, we still need a null check here for safety, though it implies init() failed catastrophically or was called concurrently.
    std::lock_guard<std::mutex> lock(s_mutex); // Protects the s_instance read, in case init is running concurrently
    if (!s_instance) {
        // This case should ideally not be reached if call_once worked and init is well-behaved.
        // If it is reached, it means s_instance was nulled after call_once's block (e.g. init failed badly).
        // Throwing an exception is better than returning a reference to a null-derived object.
        std::cerr << "CRITICAL_ERROR: Logger::s_instance is null in getInstance even after call_once. This indicates a severe issue." << std::endl;
        throw std::runtime_error("Logger instance is unexpectedly null in getInstance.");
    }
    return *s_instance;
}

// Constructor (was private, now effectively public via static init, but keep private for direct instantiation control)
Logger::Logger(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal)
    : currentLogLevel(level), logFilePath(logFile), maxFileSize(maxFileSizeVal), maxBackupFiles(maxBackupFilesVal) {
    if (logFile == CONSOLE_ONLY_OUTPUT) {
        // This is console-only mode, logFilePath is set to the special value,
        // and logFileStream is not opened (remains in its default closed state).
        // std::cerr might be used for a bootstrap message if needed, but getInstance already does.
    } else {
        // Normal file logging mode
        logFileStream.open(logFilePath, std::ios::app);
        if (!logFileStream.is_open()) {
            // Using std::cerr here as logger might not be fully functional for itself yet,
            // or if this instance is the one being created by emergency init.
            std::cerr << "Error: Could not open log file: " << logFilePath << std::endl;
        }
    }
}

// Public Destructor
Logger::~Logger(){
	if (logFileStream.is_open()) {
		logFileStream.close();
	}
}

void Logger::setLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(s_mutex);
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
    std::lock_guard<std::mutex> lock(s_mutex);
    if (level < currentLogLevel) { // Check level after acquiring lock
        return;
    }

    if (this->logFilePath == CONSOLE_ONLY_OUTPUT) {
        // In CONSOLE_ONLY_OUTPUT mode, format and write directly to std::cout.
        // This bypasses file operations and log rotation.
        // getTimestamp, levelToString, and escapeJsonString are assumed to be safe.
        std::cout << "{\"timestamp\": \"" << getTimestamp()
                  << "\", \"level\": \"" << levelToString(level)
                  << "\", \"message\": \"" << escapeJsonString(message) << "\"}" << std::endl;
        return;
    }

    // Normal file logging logic (including rotation)
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
    // If not CONSOLE_ONLY_OUTPUT and logFileStream is not open (e.g. initial open failed),
    // the message is currently dropped silently. This could be changed if desired.
}

void Logger::logToConsole(LogLevel level, const std::string& message){
    std::lock_guard<std::mutex> lock(s_mutex);
    if (level < currentLogLevel) { // Check level after acquiring lock
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
