#include "errorcodes.h"
#include "logger.h" // Include the Logger header
#include <string>   // Required for std::to_string
#include <iostream> // Can be removed if std::cerr is confirmed unused after changes

// Note: If Logger::init() hasn't been called (e.g. these are used before main's init),
// these logs will throw a runtime_error. This implies that any part of the application
// that can call these functions must run after Logger::init().

void Networking::ThrowSocketException(int socket, int errorCode)
{
    std::string msg = "Socket Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::socketMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::socketMap.at(errorCode));
}

void Networking::ThrowBindException(int socket, int errorCode)
{
    std::string msg = "Bind Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::bindMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::bindMap.at(errorCode));
}

void Networking::ThrowListenException(int socket, int errorCode)
{
    std::string msg = "Listen Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::listenMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::listenMap.at(errorCode));
}

void Networking::ThrowAcceptException(int socket, int errorCode)
{
    std::string msg = "Accept Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::acceptMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::acceptMap.at(errorCode));
}

void Networking::ThrowSendException(int socket, int errorCode)
{
    std::string msg = "Send Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::sendMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::sendMap.at(errorCode));
}

void Networking::ThrowReceiveException(int socket, int errorCode)
{
    std::string msg = "Receive Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::receiveMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
    try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::receiveMap.at(errorCode));
}

void Networking::ThrowShutdownException(int socket, int errorCode)
{
    std::string msg = "Shutdown Error. Code: " + std::to_string(errorCode) + ". Message: " + Networking::Error::shutdownMap.at(errorCode) + " (Socket: " + std::to_string(socket) + ")";
	try {
        Logger::getInstance().log(LogLevel::ERROR, msg);
    } catch (const std::runtime_error& e) {
        std::cerr << "Logger not initialized. Original error: " << msg << " Logger error: " << e.what() << std::endl;
    }
	throw Networking::NetworkException(socket, errorCode, Networking::Error::shutdownMap.at(errorCode));
}
