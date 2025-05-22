#include "logger.h"

Logger::Logger(const std::string& logFile)
{
	logFileStream.open(logFile, std::ios::app);
}

Logger::~Logger(){
	if (logFileStream.is_open()) {
		logFileStream.close();
	}
}

void Logger::log(const std::string& message){
	if (logFileStream.is_open()) {
		logFileStream << getTimestamp() << " - " << message << std::endl;
	}
}

void Logger::logToConsole(const std::string& message){
	std::cout << getTimestamp ()<< " - " << message << std::endl;
}

std::string Logger::getTimestamp(){
	std::time_t currentTime = std::time(nullptr);
	std::tm* localTime = std::localtime(&currentTime);
	char timestamp[20];
	std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",localTime);
	return std::string(timestamp);

}
