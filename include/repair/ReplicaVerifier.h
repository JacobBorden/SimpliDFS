#pragma once
#include "repair/RepairWorker.h"
#include <functional>

/**
 * @brief Verifies that all replicas of a file share the same block hash.
 */
class ReplicaVerifier {
public:
    using HashFetcher = std::function<std::string(const NodeID&, const std::string&)>;

    /**
     * @brief Construct a ReplicaVerifier.
     * @param table Reference to inode table mapping file names to replicas.
     * @param cache Health cache used to filter out dead nodes.
     * @param fetcher Callback used to retrieve the hash of a file from a node.
     */
    ReplicaVerifier(std::unordered_map<std::string, InodeEntry>& table,
                    NodeHealthCache& cache,
                    HashFetcher fetcher)
        : table_(table), cache_(cache), fetcher_(std::move(fetcher)) {}

    /**
     * @brief Verify hashes for all files in the table.
     */
    void verifyAll();

    /**
     * @brief Verify replicas for a single file.
     * @param filename Name of the file to verify.
     * @return True if all healthy replicas share the same hash.
     */
    bool verifyFile(const std::string& filename);

private:
    std::unordered_map<std::string, InodeEntry>& table_;
    NodeHealthCache& cache_;
    HashFetcher fetcher_;
};

