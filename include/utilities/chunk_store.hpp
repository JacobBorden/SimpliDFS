#ifndef CHUNK_STORE_HPP
#define CHUNK_STORE_HPP

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChunkStore {
public:
  /**
   * @brief Add a chunk to the store.
   * @param data Raw bytes that make up the chunk.
   * @return A content identifier (CID) derived from the chunk contents.
   */
  std::string addChunk(const std::vector<std::byte> &data);

  // Insert a chunk with a precomputed CID.
  void putChunk(const std::string &cid, const std::vector<std::byte> &data);

  /**
   * @brief Check if a chunk exists.
   * @param cid Content identifier returned by @ref addChunk.
   */
  bool hasChunk(const std::string &cid) const;

  /**
   * @brief Retrieve a chunk by CID.
   * @param cid Identifier of the desired chunk.
   * @return The chunk data, or an empty vector if not found.
   */
  std::vector<std::byte> getChunk(const std::string &cid) const;

  struct GCStats {
    size_t totalChunks{0};
    size_t reclaimableChunks{0};
    size_t reclaimableBytes{0};
    size_t freedChunks{0};
    size_t freedBytes{0};
  };

  /**
   * @brief Garbage collect unreferenced chunks.
   *
   * Chunks whose CIDs are not present in @p referencedCids are deleted
   * unless @p dryRun is true.  Statistics about the operation are
   * returned in all cases.
   */
  GCStats garbageCollect(const std::unordered_set<std::string> &referencedCids,
                         bool dryRun);

  /**
   * @brief Obtain a snapshot of all stored chunks.
   */
  std::unordered_map<std::string, std::vector<std::byte>> getAllChunks() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::vector<std::byte>> chunks_;
};

#endif // CHUNK_STORE_HPP
