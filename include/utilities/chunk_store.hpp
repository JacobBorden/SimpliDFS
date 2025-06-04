#ifndef CHUNK_STORE_HPP
#define CHUNK_STORE_HPP

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <cstddef>

class ChunkStore {
public:
    // Adds a chunk and returns its content-addressed hash (CID)
    std::string addChunk(const std::vector<std::byte>& data);

    // Checks if a chunk exists
    bool hasChunk(const std::string& cid) const;

    // Retrieves a chunk by CID. Returns empty vector if not found
    std::vector<std::byte> getChunk(const std::string& cid) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<std::byte>> chunks_;
};

#endif // CHUNK_STORE_HPP
