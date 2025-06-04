#include "utilities/merkle_tree.hpp"
#include "utilities/blockio.hpp"
#include <algorithm>

std::string MerkleTree::hashDirectory(const std::vector<std::pair<std::string, std::string>>& entries, ChunkStore& store) {
    std::vector<std::pair<std::string, std::string>> sorted = entries;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){ return a.first < b.first; });

    BlockIO bio;
    for (const auto& e : sorted) {
        std::vector<std::byte> name_bytes;
        for(char c : e.first) name_bytes.push_back(std::byte(c));
        bio.ingest(name_bytes.data(), name_bytes.size());
        std::vector<std::byte> hash_bytes;
        for(char c : e.second) hash_bytes.push_back(std::byte(c));
        bio.ingest(hash_bytes.data(), hash_bytes.size());
    }
    DigestResult dr = bio.finalize_hashed();
    store.addChunk(dr.raw); // store directory representation itself
    return dr.cid;
}
