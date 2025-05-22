#pragma once
#ifndef _LOGGER_H_
#define _LOGGER_H_
#include <iostream>
#include <fstream>
#include <string>
#include <ctime>

class Logger
{
public:
Logger(const std::string& logFile);
~Logger();

void log(const std::string& message);
void logToConsole(const std::string& message);
private:
std::ofstream logFileStream;
std::string getTimestamp();
};

#endif
