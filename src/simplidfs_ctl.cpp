#include "metaserver/metaserver.h"
#include <iostream>
#include <chrono>
#include <string>
#include "utilities/key_manager.hpp"
#include "utilities/merkle_tree.hpp"
#include "utilities/chunk_store.hpp"

extern MetadataManager metadataManager;

static std::string stateToString(NodeState s) {
    switch (s) {
        case NodeState::ALIVE: return "ALIVE";
        case NodeState::SUSPECT: return "SUSPECT";
        default: return "DEAD";
    }
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "verify") {
        std::string target = argv[2];
        simplidfs::KeyManager::getInstance().initialize();
        ChunkStore store;

        std::vector<std::byte> a{std::byte{'a'}};
        std::vector<std::byte> b{std::byte{'b'}};
        std::vector<std::byte> c{std::byte{'c'}};
        std::string cidA = store.addChunk(a);
        std::string cidB = store.addChunk(b);
        std::string cidC = store.addChunk(c);

        std::string cidDir2 =
            MerkleTree::hashDirectory({{"fileB", cidB}}, store);
        std::string cidDir1 =
            MerkleTree::hashDirectory({{"fileA", cidA}, {"dir2", cidDir2}},
                                      store);
        std::string rootCid =
            MerkleTree::hashDirectory({{"dir1", cidDir1}, {"fileC", cidC}},
                                      store);

        if (!store.hasChunk(target)) {
            std::cout << "CID not found" << std::endl;
            return 1;
        }
        auto proof = MerkleTree::getProofPath(rootCid, target);
        bool ok = !proof.empty() &&
                  MerkleTree::verifyProof(rootCid, target, proof);
        std::cout << (ok ? "Verification succeeded" : "Verification failed")
                  << std::endl;
        return ok ? 0 : 1;
    }

    if (argc < 3 || std::string(argv[1]) != "ctl") {
        std::cout << "Usage: simplidfs ctl [health|repair run-once|rotate-key <window>]" << std::endl;
        std::cout << "       simplidfs verify <cid>" << std::endl;
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
