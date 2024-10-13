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

void HandleClientConnection(Networking::Server _pServer, Networking::ClientConnection _pClient)
{
    Message request = DeserializeMessage(&_pServer.Receive(_pClient)[0]);
    switch (request._Type)
    {
    }
}

int main()
{
    Networking::Server server(50505);

    if (server.ServerIsRunning())
    {
        MetadataManager metadataManager;
        FileSystem fileSystem; // Using FileSystem from filesystem.h to manage file operations
        while (true)
        {
            // Accept a client connection

            Networking::ClientConnection client = server.Accept();
            std::thread clientThread(HandleClientConnection, server, client);
            clientThread.detach();
        }
    }

    return 0;
}
