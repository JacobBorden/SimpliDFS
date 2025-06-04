#include "utilities/chunk_store.hpp"
#include "utilities/blockio.hpp"

std::string ChunkStore::addChunk(const std::vector<std::byte>& data) {
    BlockIO bio;
    if (!data.empty()) {
        bio.ingest(data.data(), data.size());
    }
    DigestResult dr = bio.finalize_hashed();
    std::lock_guard<std::mutex> lock(mutex_);
    chunks_[dr.cid] = dr.raw;
    return dr.cid;
}

bool ChunkStore::hasChunk(const std::string& cid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return chunks_.count(cid) > 0;
}

std::vector<std::byte> ChunkStore::getChunk(const std::string& cid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = chunks_.find(cid);
    if (it != chunks_.end()) {
        return it->second;
    }
    return {};
}
