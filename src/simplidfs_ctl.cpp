#include "metaserver/metaserver.h"
#include "utilities/chunk_store.hpp"
#include "utilities/key_manager.hpp"
#include "utilities/merkle_tree.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

extern MetadataManager metadataManager;

static std::string stateToString(NodeState s) {
  switch (s) {
  case NodeState::ALIVE:
    return "ALIVE";
  case NodeState::SUSPECT:
    return "SUSPECT";
  default:
    return "DEAD";
  }
}

int main(int argc, char **argv) {
  if (argc < 3 || std::string(argv[1]) != "ctl") {
    std::cout << "Usage: simplidfs ctl [health|repair run-once|rotate-key "
                 "<window>|verify <cid> [chunk_dir]]\n";
    return 1;
  }
  std::string cmd = argv[2];
  if (cmd == "health") {
    auto snap = metadataManager.getHealthSnapshot();
    auto now = SteadyClock::now();
    std::cout << "Node\tState\tLastChangeAgo" << std::endl;
    for (const auto &kv : snap) {
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
                     now - kv.second.lastChange)
                     .count();
      std::cout << kv.first << '\t' << stateToString(kv.second.state) << '\t'
                << age << "s" << std::endl;
    }
    return 0;
  } else if (cmd == "repair" && argc >= 4 &&
             std::string(argv[3]) == "run-once") {
    // Placeholder: real repair logic would hook into MetadataManager
    std::cout << "Repair run-once triggered" << std::endl;
    return 0;
  } else if (cmd == "rotate-key" && argc >= 4) {
    unsigned int window = std::stoul(argv[3]);
    simplidfs::KeyManager::getInstance().rotateClusterKey(window);
    std::cout << "Cluster key rotated. Previous key valid for " << window
              << " seconds." << std::endl;
    return 0;
  } else if (cmd == "verify" && argc >= 4) {
    std::string cid = argv[3];
    std::string dir = (argc >= 5) ? argv[4] : "chunks";

    ChunkStore store;
    auto loadChunk = [&](const std::string &c) {
      std::ifstream in(dir + "/" + c + ".bin", std::ios::binary);
      if (!in.is_open())
        return false;
      std::vector<char> tmp((std::istreambuf_iterator<char>(in)), {});
      std::vector<std::byte> data(tmp.size());
      std::transform(tmp.begin(), tmp.end(), data.begin(),
                     [](char ch) { return std::byte(ch); });
      store.addChunk(data);
      return true;
    };

    if (!loadChunk(cid)) {
      std::cout << "Missing chunk for CID: " << cid << std::endl;
      return 1;
    }

    std::vector<std::string> path{cid};
    std::ifstream pf(dir + "/" + cid + ".proof");
    std::string line;
    while (std::getline(pf, line)) {
      if (!loadChunk(line)) {
        std::cout << "Missing chunk for CID: " << line << std::endl;
        return 1;
      }
      path.push_back(line);
    }

    bool ok = MerkleTree::verifyProof(path, store);
    std::cout << (ok ? "Verification succeeded" : "Verification failed")
              << std::endl;
    return ok ? 0 : 1;
  }
  std::cout << "Unknown command" << std::endl;
  return 1;
}
