// Metadata Management for SimpliDFS
// Let's create a metadata service that will act as the core to track file blocks, replication, and file locations.

#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include "filesystem.h"
#include "message.h" // Including message for node communication
#include <sstream>
#include "metaserver.h"
#include "server.h"
#include <thread>

Networking::Server server(50505);
MetadataManager metadataManager;

void HandleClientConnection(Networking::ClientConnection _pClient)
{
    Message request = DeserializeMessage(&server.Receive(_pClient)[0]);
    switch (request._Type)
    {
    case MessageType::CreateFile:
    {
        std::vector<std::string> nodes;
        metadataManager.addFile(request._Filename, nodes);
        break;
    }

    case MessageType::ReadFile:
    {
        std::vector<std::string> nodes = metadataManager.getFileNodes(request._Filename);
        break;
    }

    case MessageType::WriteFile:
    {
        std::vector<std::string> nodes = metadataManager.getFileNodes(request._Filename);
        break;
    }
    }
}

int main()
{

    if (server.ServerIsRunning())
    {

        FileSystem fileSystem; // Using FileSystem from filesystem.h to manage file operations
        while (true)
        {
            // Accept a client connection

            Networking::ClientConnection client = server.Accept();
            std::thread clientThread(HandleClientConnection, client);
            clientThread.detach();
        }
    }

    return 0;
}
