# BlockIO Abstraction Prototype

## Overview

The `BlockIO` class provides a simple streaming interface for ingesting data and retrieving it as a contiguous block. In its current prototype form, it acts as a buffer that concatenates all ingested data chunks. Future enhancements will allow for various data processing stages to be hooked into the pipeline.

## Current API

The `BlockIO` class is defined in `src/blockio.hpp` and implemented in `src/blockio.cpp`.

```cpp
#include <vector>
#include <span>
#include <cstddef> // For std::byte

class BlockIO {
public:
    // Appends a span of constant bytes to an internal buffer.
    void ingest(std::span<const std::byte> data);

    // Returns a vector containing all ingested data, concatenated in order.
    // In the current version, this is the raw, unprocessed data.
    std::vector<std::byte> finalize_raw();
private:
    std::vector<std::byte> buffer_; // Internal buffer to store data
};
```

### Usage

1.  Create a `BlockIO` object.
2.  Call `ingest()` one or more times to feed data into the object.
3.  Call `finalize_raw()` to retrieve all the data as a single `std::vector<std::byte>`.


## Future Pipeline Hooks (Conceptual)

The `BlockIO` abstraction is designed to be extensible. The vision is to allow a chain of processing stages to be configured. Data ingested would flow through these stages before being finalized.

Below are some conceptual examples of how such hooks might be defined and used. The exact API and implementation details are subject to further design.

### Potential Transform Stages:

*   **Hashing:** Calculate a cryptographic hash of the data.
*   **Compression:** Compress the data to reduce its size.
*   **Encryption:** Encrypt the data for confidentiality.

### Example Conceptual API Extensions:

```cpp
// Hypothetical future extensions to the BlockIO class
class BlockIO {
public:
    // ... existing methods ...

    // --- Configuration for Processing Stages ---

    // enum class HashAlgorithm { SHA256, SHA512, BLAKE3 };
    // void add_hash_transform(HashAlgorithm algo);

    // enum class CompressionAlgorithm { ZSTD, LZ4, GZIP };
    // void add_compression_transform(CompressionAlgorithm algo, int level = 0 /* default */);

    // enum class EncryptionAlgorithm { AES_GCM_256 /* , ChaChaPoly */ };
    // struct EncryptionKey { /* Opaque key material */ };
    // void add_encryption_transform(EncryptionAlgorithm algo, EncryptionKey key);

    // --- Output / Finalization ---

    // Interface for different output destinations (e.g., file, network)
    // class OutputSink {
    // public:
    //     virtual ~OutputSink() = default;
    //     virtual void write(std::span<const std::byte> data_block) = 0;
    //     virtual void close() = 0;
    // };
    // void set_output_sink(std::unique_ptr<OutputSink> sink);

    // Finalizes processing and writes to the configured sink,
    // or returns processed data if no sink (or a default memory sink) is set.
    // Might return metadata (e.g., hash, final size).
    // struct ProcessedOutput {
    //     std::vector<std::byte> data; // If applicable
    //     std::vector<std::byte> hash_digest; // If hashing was enabled
    //     // Other metadata...
    // };
    // ProcessedOutput finalize_processed(); 
};
```

### Data Flow Example:

1.  User configures the `BlockIO` instance:
    *   `bio.add_hash_transform(HashAlgorithm::SHA256);`
    *   `bio.add_compression_transform(CompressionAlgorithm::ZSTD);`
    *   `bio.set_output_sink(std::make_unique<MyFileSink>("/path/to/output.dat"));`
2.  User calls `bio.ingest(data_chunk_1);`, `bio.ingest(data_chunk_2);` etc.
    *   Internally, data might be processed per chunk or buffered up to a certain point before being pushed through the pipeline stages.
3.  User calls `bio.finalize_processed();` (or a similar method).
    *   The (remaining) data is pushed through the configured stages:
        1.  Hashing (e.g., SHA256 is calculated on the plaintext).
        2.  Compression (e.g., ZSTD is applied to the plaintext).
        3.  (Encryption could be another stage if added).
    *   The final (e.g., compressed) data is written to the `MyFileSink`.
    *   The method might return the calculated hash.

This modular design would allow for flexible and efficient data processing pipelines tailored to specific needs. The current `finalize_raw()` serves as the baseline "pass-through" behavior.
