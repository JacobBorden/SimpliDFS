# Concurrency Test Ideas for MetadataManager and FileSystem

## Objective:
Test concurrent file creation, deletion, registration, heartbeat processing, and metadata updates in `MetadataManager`, as well as basic file operations in `FileSystem`, to ensure thread safety and data consistency under load.

## Setup:

1.  **Instantiate `MetadataManager`**: One instance will be shared across multiple threads.
2.  **Instantiate `FileSystem`**: One instance will be shared (though most tests here focus on `MetadataManager` which uses `FileSystem` indirectly via nodes, direct `FileSystem` tests could also be designed).
3.  **Define Operations for Threads**:
    *   **Thread Type A (File Creator/Deleter):**
        1.  Call `metadataManager.addFile("file_A_threadX", {"node1", "node2"})`.
        2.  (Simulate node actions: nodes would call `fileSystem.createFile` then `fileSystem.writeFile`).
        3.  Call `metadataManager.removeFile("file_A_threadX")`.
        4.  (Simulate node actions: nodes would call `fileSystem.deleteFile`).
    *   **Thread Type B (Node Registrator & File Assigner):**
        1.  Call `metadataManager.registerNode("node_threadY", "192.168.1.Y", 808Y)`.
        2.  Call `metadataManager.addFile("file_B_threadY", {"node_threadY"})`.
    *   **Thread Type C (Metadata Querier):**
        1.  Repeatedly (e.g., in a loop with short sleeps) call `metadataManager.getFileNodes("file_A_threadX")` (expecting it to sometimes exist, sometimes not).
        2.  Repeatedly call `metadataManager.getFileNodes("file_B_threadY")`.
        3.  Handle potential exceptions if a file is queried before creation or after deletion.
    *   **Thread Type D (Metadata Dumper):**
        1.  Repeatedly call `metadataManager.printMetadata()`.
    *   **Thread Type E (Heartbeat Sender):**
        1.  For a registered node (e.g., "node_threadY"), repeatedly call `metadataManager.processHeartbeat("node_threadY")`.
    *   **Thread Type F (Dead Node Checker - if run by MetaServer internally, this is more about stressing it):**
        1.  Repeatedly call `metadataManager.checkForDeadNodes()`. (This method has internal locking, so calling it concurrently is a test of its own robustness, though in the current design it's called by the MetaServer's main loop or a dedicated thread).
    *   **Thread Type G (Saver/Loader Stress - less common for direct concurrent access but tests robustness):**
        1.  Thread G1: Repeatedly call `metadataManager.saveMetadata("test_fm.dat", "test_nr.dat")`.
        2.  Thread G2: Repeatedly call `metadataManager.loadMetadata("test_fm.dat", "test_nr.dat")` (this is dangerous if not properly synchronized with savers and other operations, usually load is at startup). *This specific concurrent load/save test would require careful design or might be omitted in favor of testing save/load atomicity separately.*

## Execution:

1.  Create a `MetadataManager` instance.
2.  Launch multiple `std::thread` instances (e.g., 2-4 of each Thread Type A-E, and 1 of Type F if testing its concurrency).
3.  Let threads run concurrently for a certain duration or a fixed number of operations.
4.  Use `std::vector<std::thread>` to manage threads and `join()` them all at the end.
5.  Employ `std::atomic` counters for operations or errors if fine-grained tracking is needed.
6.  For specific interleavings (e.g., delete a file exactly when another thread is trying to read its metadata), `std::condition_variable` and `std::mutex` could be used to orchestrate, but general concurrent execution is a good first step.

## Validation:

1.  **No Crashes/Deadlocks:** The primary goal is that the program completes without segmentation faults, deadlocks, or other concurrency-related crashes.
2.  **Data Consistency (Post-Execution):**
    *   Call `metadataManager.printMetadata()` one last time.
    *   Inspect the final state of `fileMetadata` and `registeredNodes`.
        *   Files created and then deleted should not be in `fileMetadata`.
        *   Nodes registered should be in `registeredNodes`.
        *   File-to-node assignments should be consistent with operations performed.
        *   If `saveMetadata` and `loadMetadata` were part of the test, verify the persisted files reflect a consistent final state.
    *   If `FileSystem` was directly manipulated, check its internal state or mock its behavior to verify calls.
3.  **Log Analysis:**
    *   Examine console output for error messages (e.g., "Error: No live nodes available", "Warning: Could not find a new live node", "File not found in metadata.").
    *   Ensure no unexpected error states are reported due to race conditions.
4.  **Specific Checks:**
    *   For Thread Type C, ensure it doesn't crash when a file suddenly appears or disappears. Exceptions for "file not found" are expected and should be handled gracefully.
    *   If `checkForDeadNodes` and `processHeartbeat` are run concurrently, ensure `isAlive` status and `lastHeartbeat` times are logical.

## Example Snippet (Conceptual):

```cpp
// In a test file, e.g., test_concurrency.cpp

#include "metaserver.h" // Assuming MetadataManager is here
#include <thread>
#include <vector>
#include <iostream>
#include <string>
#include <atomic>

// Shared instance
MetadataManager metadataManager;
std::atomic<int> fileCreationAttempts(0);
std::atomic<int> fileDeletionAttempts(0);

void worker_create_delete_file(int thread_id) {
    std::string filename = "test_file_" + std::to_string(thread_id) + ".txt";
    std::vector<std::string> nodes = {"node1", "node2"}; // Assume node1, node2 are known/registered

    for (int i = 0; i < 5; ++i) { // Perform a few operations
        std::cout << "Thread " << thread_id << ": Attempting to add file " << filename << std::endl;
        metadataManager.addFile(filename, nodes);
        fileCreationAttempts++;
        // Simulate some work or delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        std::cout << "Thread " << thread_id << ": Attempting to remove file " << filename << std::endl;
        metadataManager.removeFile(filename);
        fileDeletionAttempts++;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void worker_register_node(int thread_id) {
    std::string nodeID = "node_T" + std::to_string(thread_id);
    std::string nodeAddr = "10.0.0." + std::to_string(thread_id);
    int port = 9000 + thread_id;

    for (int i = 0; i < 3; ++i) {
        std::cout << "Thread " << thread_id << ": Registering node " << nodeID << std::endl;
        metadataManager.registerNode(nodeID, nodeAddr, port);
        // Simulate heartbeats for this node
        metadataManager.processHeartbeat(nodeID);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}


int main_concurrency_test() { // Renamed to avoid conflict if it were real main
    std::vector<std::thread> threads;
    const int num_creator_threads = 5;
    const int num_register_threads = 3;

    // Create threads
    for (int i = 0; i < num_creator_threads; ++i) {
        threads.emplace_back(worker_create_delete_file, i);
    }
    for (int i = 0; i < num_register_threads; ++i) {
        threads.emplace_back(worker_register_node, num_creator_threads + i);
    }

    // Join threads
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All threads completed." << std::endl;
    std::cout << "File creation attempts: " << fileCreationAttempts << std::endl;
    std::cout << "File deletion attempts: " << fileDeletionAttempts << std::endl;

    metadataManager.printMetadata();
    // Further assertions on the state of metadataManager.fileMetadata and metadataManager.registeredNodes
    // For example, check if all created then deleted files are indeed gone.
    // Check if all registered nodes are present.

    // Save final state for inspection
    metadataManager.saveMetadata("final_fm_concurrency_test.dat", "final_nr_concurrency_test.dat");

    return 0;
}

```

This conceptual test focuses on the `MetadataManager`. Similar tests could be designed for `FileSystem` directly, involving multiple threads calling `createFile`, `writeFile`, `readFile`, and `deleteFile` on overlapping and distinct filenames.
