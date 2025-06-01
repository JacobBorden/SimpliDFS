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

bool FileSystem::renameFile(const std::string& _pOldFilename, const std::string& _pNewFilename) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.find(_pOldFilename) == _Files.end()) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to rename non-existent file: " + _pOldFilename);
        return false; // Old file doesn't exist
    }
    if (_Files.find(_pNewFilename) != _Files.end()) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to rename to an already existing file: " + _pNewFilename);
        return false; // New file already exists
    }

    // Perform the rename
    _Files[_pNewFilename] = _Files[_pOldFilename];
    _Files.erase(_pOldFilename);

    Logger::getInstance().log(LogLevel::INFO, "File renamed from " + _pOldFilename + " to " + _pNewFilename);
    return true;
}

void FileSystem::setXattr(const std::string& filename, const std::string& attrName, const std::string& attrValue) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.find(filename) == _Files.end()) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to set xattr for non-existent file: " + filename);
        return;
    }
    _FileXattrs[filename][attrName] = attrValue;
    Logger::getInstance().log(LogLevel::INFO, "xattr set for file: " + filename + ", Attribute: " + attrName);
}

std::string FileSystem::getXattr(const std::string& filename, const std::string& attrName) {
    std::unique_lock<std::mutex> lock(_Mutex);
    if (_Files.find(filename) == _Files.end()) {
        Logger::getInstance().log(LogLevel::WARN, "Attempted to get xattr for non-existent file: " + filename);
        return "";
    }
    auto it = _FileXattrs.find(filename);
    if (it != _FileXattrs.end()) {
        auto attrIt = it->second.find(attrName);
        if (attrIt != it->second.end()) {
            Logger::getInstance().log(LogLevel::INFO, "xattr retrieved for file: " + filename + ", Attribute: " + attrName);
            return attrIt->second;
        }
    }
    Logger::getInstance().log(LogLevel::INFO, "xattr not found for file: " + filename + ", Attribute: " + attrName);
    return "";
}
