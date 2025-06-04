#ifndef CHUNK_STORE_HPP
#define CHUNK_STORE_HPP

#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <cstddef>
#include <unordered_set>

class ChunkStore {
public:
    // Adds a chunk and returns its content-addressed hash (CID)
    std::string addChunk(const std::vector<std::byte>& data);

    // Checks if a chunk exists
    bool hasChunk(const std::string& cid) const;

    // Retrieves a chunk by CID. Returns empty vector if not found
    std::vector<std::byte> getChunk(const std::string& cid) const;

    struct GCStats {
        size_t totalChunks{0};
        size_t reclaimableChunks{0};
        size_t reclaimableBytes{0};
        size_t freedChunks{0};
        size_t freedBytes{0};
    };

    // Remove any chunks not present in referencedCids. When dryRun is true,
    // the chunks are left untouched but stats report what would be reclaimed.
    GCStats garbageCollect(const std::unordered_set<std::string>& referencedCids,
                           bool dryRun);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<std::byte>> chunks_;
};

#endif // CHUNK_STORE_HPP
