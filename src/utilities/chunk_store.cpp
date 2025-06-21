#include "utilities/chunk_store.hpp"
#include "utilities/blockio.hpp"

std::string ChunkStore::addChunk(const std::vector<std::byte> &data) {
  BlockIO bio;

  // Feed the data into the digest calculator. Empty chunks are
  // still hashed to produce a unique CID.
  if (!data.empty()) {
    bio.ingest(data.data(), data.size());
  }

  // Finalize and obtain both the CID and raw bytes.
  DigestResult dr = bio.finalize_hashed();

  // Store the chunk thread-safely.
  std::lock_guard<std::mutex> lock(mutex_);
  chunks_[dr.cid] = dr.raw;
  return dr.cid;
}

void ChunkStore::putChunk(const std::string &cid,
                          const std::vector<std::byte> &data) {
  std::lock_guard<std::mutex> lock(mutex_);
  chunks_[cid] = data;
}

bool ChunkStore::hasChunk(const std::string &cid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return chunks_.count(cid) > 0;
}

std::vector<std::byte> ChunkStore::getChunk(const std::string &cid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = chunks_.find(cid);
  if (it != chunks_.end()) {
    return it->second;
  }
  return {};
}

ChunkStore::GCStats ChunkStore::garbageCollect(
    const std::unordered_set<std::string> &referencedCids, bool dryRun) {
  std::lock_guard<std::mutex> lock(mutex_);
  GCStats stats{};
  stats.totalChunks = chunks_.size();

  // Iterate over all stored chunks and remove those not referenced
  for (auto it = chunks_.begin(); it != chunks_.end();) {
    if (referencedCids.count(it->first) == 0) {
      stats.reclaimableChunks++;
      stats.reclaimableBytes += it->second.size();
      if (!dryRun) {
        stats.freedChunks++;
        stats.freedBytes += it->second.size();
        it = chunks_.erase(it);
        continue;
      }
    }
    ++it;
  }
  return stats;
}

std::unordered_map<std::string, std::vector<std::byte>>
ChunkStore::getAllChunks() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return chunks_;
}
