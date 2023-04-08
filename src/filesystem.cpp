#include "filesystem.h"

bool FileSystem::createFile(const std::string& _pFilename)
{
	std::unique_lock<std::mutex> lock(_Mutex);
	if(_Files.count(_pFilename))
		return false;
	_Files[_pFilename] = "";
	return true;
		
}
