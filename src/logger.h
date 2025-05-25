#pragma once
#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>
#include <stdexcept> // Required for std::runtime_error

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

    static void init(const std::string& logFile, LogLevel level = LogLevel::INFO, long long maxFileSize = 10 * 1024 * 1024, int maxBackupFiles = 5);
    static Logger& getInstance();

    void setLogLevel(LogLevel level);
    void log(LogLevel level, const std::string& message);
    void logToConsole(LogLevel level, const std::string& message);

private:
    Logger(const std::string& logFile, LogLevel level, long long maxFileSizeVal, int maxBackupFilesVal); // Constructor
    ~Logger();

    std::string getTimestamp();
    std::string levelToString(LogLevel level);

    std::ofstream logFileStream;
    LogLevel currentLogLevel;
    std::string logFilePath;
    long long maxFileSize;
    int maxBackupFiles;

    // Static members for init and getInstance
    static std::string s_filePath;
    static LogLevel s_initialLevel;
    static long long s_maxSize;
    static int s_backups;
    static bool s_isInitialized;
};

#endif
