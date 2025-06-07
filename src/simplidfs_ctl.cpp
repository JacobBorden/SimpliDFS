#include "metaserver/metaserver.h"
#include <iostream>
#include <chrono>
#include <string>
#include "utilities/key_manager.hpp"

extern MetadataManager metadataManager;

static std::string stateToString(NodeState s) {
    switch (s) {
        case NodeState::ALIVE: return "ALIVE";
        case NodeState::SUSPECT: return "SUSPECT";
        default: return "DEAD";
    }
}

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "ctl") {
        std::cout << "Usage: simplidfs ctl [health|repair run-once|rotate-key <window>]\n";
        return 1;
    }
    std::string cmd = argv[2];
    if (cmd == "health") {
        auto snap = metadataManager.getHealthSnapshot();
        auto now = SteadyClock::now();
        std::cout << "Node\tState\tLastChangeAgo" << std::endl;
        for (const auto& kv : snap) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - kv.second.lastChange).count();
            std::cout << kv.first << '\t' << stateToString(kv.second.state) << '\t' << age << "s" << std::endl;
        }
        return 0;
    } else if (cmd == "repair" && argc >= 4 && std::string(argv[3]) == "run-once") {
        // Placeholder: real repair logic would hook into MetadataManager
        std::cout << "Repair run-once triggered" << std::endl;
        return 0;
    } else if (cmd == "rotate-key" && argc >= 4) {
        unsigned int window = std::stoul(argv[3]);
        simplidfs::KeyManager::getInstance().rotateClusterKey(window);
        std::cout << "Cluster key rotated. Previous key valid for " << window << " seconds." << std::endl;
        return 0;
    }
    std::cout << "Unknown command" << std::endl;
    return 1;
}
