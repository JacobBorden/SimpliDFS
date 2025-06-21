#include "metaserver/metaserver.h"
#include "utilities/chunk_store.hpp"
#include "utilities/key_manager.hpp"
#include "utilities/merkle_tree.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

static int verify_command(const std::string &cid, const std::string &dir) {
  namespace fs = std::filesystem;
  ChunkStore store;
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    std::string name = entry.path().filename();
    if (name.size() > 6 && name.substr(name.size() - 6) == ".proof")
      continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::vector<char> tmp((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    std::vector<std::byte> data;
    data.reserve(tmp.size());
    for (char c : tmp)
      data.push_back(std::byte(c));
    store.putChunk(name, data);
  }
  std::string proofFile = (fs::path(dir) / (cid + ".proof")).string();
  std::ifstream pf(proofFile);
  if (!pf.is_open()) {
    std::cout << "Proof file not found" << std::endl;
    return 1;
  }
  std::vector<std::string> proof;
  std::string line;
  while (std::getline(pf, line)) {
    if (!line.empty())
      proof.push_back(line);
  }
  bool ok = MerkleTree::verifyProof(cid, proof, store);
  std::cout << (ok ? "Verification succeeded" : "Verification FAILED")
            << std::endl;
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc >= 3 && std::string(argv[1]) == "verify") {
    std::string cid = argv[2];
    std::string dir = (argc >= 4) ? argv[3] : "chunks";
    return verify_command(cid, dir);
  }
  if (argc < 3 || std::string(argv[1]) != "ctl") {
    std::cout
        << "Usage: simplidfs ctl [health|repair run-once|rotate-key <window>]";
    std::cout << "\n       simplidfs verify <cid> [chunk_dir]\n";
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
  }
  std::cout << "Unknown command" << std::endl;
  return 1;
}
