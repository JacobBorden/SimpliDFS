#include "filesystem.h"

bool FileSystem::createFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(_Files.count(_pFilename))
		return false;
	_Files[_pFilename] = "";
	return true;
		
}


bool FileSystem::writeFile(const std::string& _pFilename, const std::string& _pContent)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename))
		return false;
	_Files[_pFilename] = _pContent;
	return true;

}


std::string FileSystem::readFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(!_Files.count(_pFilename))
		return "";
	return _Files[_pFilename];
}
