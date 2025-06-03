#pragma once
#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <stdexcept> // Required for std::runtime_error
#include <mutex>     // For std::mutex and std::lock_guard

enum LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger
{
public:
    // Deleting copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static const std::string CONSOLE_ONLY_OUTPUT; // Special value for console-only logging

    static void init(const std::string& logFile, LogLevel level = LogLevel::INFO, long long maxFileSize = 10 * 1024 * 1024, int maxBackupFiles = 5);
    static Logger& getInstance();

    void setLogLevel(LogLevel level);
    void log(LogLevel level, const std::string& message);
    void logToConsole(LogLevel level, const std::string& message);

private:
    Logger(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal); // Private Constructor
public: // Public Destructor
    ~Logger();
private:
    std::string getTimestamp();
    std::string levelToString(LogLevel level);

    std::ofstream logFileStream;
    LogLevel currentLogLevel;
    std::string logFilePath;
    long long maxFileSize;
    int maxBackupFiles;

    // Static members for init and getInstance
    static Logger* s_instance; 
    static std::mutex s_mutex; // Mutex for thread safety
    static std::once_flag s_once_flag; // For robust singleton initialization
};

#endif
