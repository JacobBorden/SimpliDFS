#include "utilities/filesystem.h"
#include "utilities/logger.h" // Include the Logger header
#include <string>   // For std::string, though often included by iostream/filesystem.h

bool FileSystem::createFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to create file that already exists: " + _pFilename);
		return false;
    }
	_Files[_pFilename] = "";
    Logger::getInstance().log(LogLevel::INFO, "File created: " + _pFilename);
	return true;
		
}


bool FileSystem::writeFile(const std::string& _pFilename, const std::string& _pContent)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::ERROR, "Attempted to write to non-existent file: " + _pFilename);
		return false;
    }
	_Files[_pFilename] = _pContent;
    Logger::getInstance().log(LogLevel::INFO, "File written: " + _pFilename + ", Content length: " + std::to_string(_pContent.length()));
	return true;

}


std::string FileSystem::readFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename)) {
        Logger::getInstance().log(LogLevel::ERROR, "Attempted to read non-existent file: " + _pFilename);
		return "";
    }
    Logger::getInstance().log(LogLevel::INFO, "File read: " + _pFilename);
	return _Files[_pFilename];
}

bool FileSystem::deleteFile(const std::string& _pFilename) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.count(_pFilename)) {
        _Files.erase(_pFilename);
        Logger::getInstance().log(LogLevel::INFO, "File deleted: " + _pFilename);
        return true; // Successfully deleted
    }
    Logger::getInstance().log(LogLevel::WARN, "Attempted to delete non-existent file: " + _pFilename);
    return false; // File did not exist
}
