#include "../src/blockio.hpp" // Adjust path if necessary based on include dirs
#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>   // For std::iota if used for data generation
#include <cstdlib>   // For std::rand, std::srand
#include <algorithm> // For std::generate
#include <ctime>     // For std::time

// Helper function to create a vector of bytes with random values
std::vector<std::byte> create_random_byte_vector(size_t size) {
    std::vector<std::byte> vec(size);
    // std::srand(static_cast<unsigned int>(std::time(nullptr))); // Seed random number generator
    // Seeding here will cause all chunks to be identical if called rapidly.
    // Seed once in main or ensure enough time passes for std::time to change.
    std::generate(vec.begin(), vec.end(), []() {
        return std::byte(std::rand() % 256);
    });
    return vec;
}

int main() {
    // Seed random number generator once for the entire program run
    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    const size_t total_size_bytes = 1ULL * 1024 * 1024 * 1024; // 1 GiB
    const size_t chunk_size_bytes = 1 * 1024 * 1024;       // 1 MiB
    const size_t num_chunks = total_size_bytes / chunk_size_bytes;

    std::cout << "Preparing 1 GiB of data in " << num_chunks << " chunks of " << chunk_size_bytes / (1024*1024) << " MiB each..." << std::endl;
    std::vector<std::vector<std::byte>> source_chunks;
    source_chunks.reserve(num_chunks);
    for (size_t i = 0; i < num_chunks; ++i) {
        source_chunks.push_back(create_random_byte_vector(chunk_size_bytes));
    }
    std::cout << "Data preparation complete." << std::endl;

    BlockIO bio;
    std::vector<std::byte> result_data; // To store finalized data

    // --- Benchmarking Ingest ---
    auto ingest_start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_chunks; ++i) {
        bio.ingest(std::span<const std::byte>(source_chunks[i]));
    }
    auto ingest_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> ingest_duration = ingest_end_time - ingest_start_time;

    // --- Benchmarking finalize_raw ---
    auto finalize_start_time = std::chrono::high_resolution_clock::now();
    result_data = bio.finalize_raw();
    auto finalize_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> finalize_duration = finalize_end_time - finalize_start_time;
    
    std::chrono::duration<double> total_duration = ingest_duration + finalize_duration;

    // Verification (optional, but good for sanity check)
    // This can be very slow for 1GiB, consider a smaller verification or hashing.
    // For now, we'll just check the size.
    if (result_data.size() != total_size_bytes) {
        std::cerr << "Error: Finalized data size (" << result_data.size()
                  << ") does not match total input size (" << total_size_bytes
                  << ")." << std::endl;
        // Optionally, compare content for smaller data sets if feasible:
        // size_t current_pos = 0;
        // bool verified = true;
        // for (size_t i = 0; i < num_chunks; ++i) {
        //     if (current_pos + source_chunks[i].size() > result_data.size() ||
        //         !std::equal(source_chunks[i].begin(), source_chunks[i].end(), result_data.begin() + current_pos)) {
        //         verified = false;
        //         std::cerr << "Data mismatch in chunk " << i << std::endl;
        //         break;
        //     }
        //     current_pos += source_chunks[i].size();
        // }
        // if(verified) std::cout << "Data content verified (for checked portion)." << std::endl;

    } else {
        std::cout << "Finalized data size matches input size." << std::endl;
    }


    double total_mb = static_cast<double>(total_size_bytes) / (1024 * 1024); // Corrected to total_mb for MiB calculation
    double total_gb = static_cast<double>(total_size_bytes) / (1024 * 1024 * 1024);


    std::cout << "--- BlockIO Benchmark Results ---" << std::endl;
    std::cout << "Total data processed: " << total_gb << " GiB (" << total_size_bytes << " bytes)" << std::endl; // Display in GiB
    std::cout << "Ingest time: " << ingest_duration.count() << " seconds" << std::endl;
    std::cout << "Finalize_raw time: " << finalize_duration.count() << " seconds" << std::endl;
    std::cout << "Total BlockIO operation time: " << total_duration.count() << " seconds" << std::endl;
    
    if (total_duration.count() > 0) {
        double throughput_mib_s = total_mb / total_duration.count(); // Throughput in MiB/s
        std::cout << "Throughput: " << throughput_mib_s << " MiB/s" << std::endl;
    } else {
        std::cout << "Throughput: N/A (duration was zero)" << std::endl;
    }

    // Optional: Compare with memcpy (simplified)
    // Note: This isn't a perfect apples-to-apples comparison as BlockIO does more (vector reallocations etc.)
    // std::vector<std::byte> memcpy_buffer;
    // memcpy_buffer.reserve(total_size_bytes); // Pre-allocate to be fair
    // auto memcpy_start_time = std::chrono::high_resolution_clock::now();
    // for (size_t i = 0; i < num_chunks; ++i) {
    //     memcpy_buffer.insert(memcpy_buffer.end(), source_chunks[i].begin(), source_chunks[i].end());
    // }
    // auto memcpy_end_time = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double> memcpy_duration = memcpy_end_time - memcpy_start_time;
    // std::cout << "--- Reference memcpy-like copy (vector::insert) ---" << std::endl;
    // std::cout << "Total data copied: " << total_mb << " MiB" << std::endl;
    // std::cout << "Time: " << memcpy_duration.count() << " seconds" << std::endl;
    // if (memcpy_duration.count() > 0) {
    //     double memcpy_throughput_mb_s = total_mb / memcpy_duration.count();
    //     std::cout << "Throughput: " << memcpy_throughput_mb_s << " MiB/s" << std::endl;
    // } else {
    //    std::cout << "Throughput: N/A (duration was zero)" << std::endl;
    // }
    // if (memcpy_duration.count() > 0 && total_duration.count() > 0 && (total_mb / memcpy_duration.count()) > 0) { // Avoid division by zero
    //    double throughput_mb_s = total_mb / total_duration.count();
    //    double memcpy_throughput_mb_s = total_mb / memcpy_duration.count();
    //    if (memcpy_throughput_mb_s > 0) { // Final check
    //        std::cout << "BlockIO throughput is " << (throughput_mb_s / memcpy_throughput_mb_s * 100.0) << "% of vector::insert throughput." << std::endl;
    //    }
    //}


    return 0;
}
