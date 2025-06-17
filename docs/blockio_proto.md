# BlockIO Abstraction Prototype

## Overview

The `BlockIO` class provides a simple streaming interface for ingesting data and retrieving it as a contiguous block. Originally it only buffered raw data. It now supports an optional pipeline of hashing, compression and encryption steps that can be enabled at runtime.

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
    // This bypasses the processing pipeline.
    std::vector<std::byte> finalize_raw();

    // Configure processing stages
    void enable_hashing(bool enable);
    void enable_compression(bool enable);
    void enable_encryption(bool enable);

    struct PipelineResult {
        std::vector<std::byte> data;
        std::vector<unsigned char> nonce;
        std::array<uint8_t, 32> digest;
        std::string cid;
    };

    // Run the configured pipeline on the buffered data.
    PipelineResult finalize_pipeline(const std::array<unsigned char, 32>* key = nullptr);
private:
    std::vector<std::byte> buffer_; // Internal buffer to store data
};
```

### Usage

1.  Create a `BlockIO` object.
2.  Call `ingest()` one or more times to feed data into the object.
3.  Call `finalize_raw()` to retrieve all the data as a single `std::vector<std::byte>`.


## Pipeline Processing

`BlockIO` can chain compression, encryption and hashing in that order. Each stage is optional and controlled through the `enable_*` methods shown above. The `finalize_pipeline()` function runs the configured stages and returns the processed data along with metadata such as the SHA‑256 digest, CID and encryption nonce if applicable.

## Runtime Configuration

At runtime the system allows tuning of two processing knobs:

* **Compression level** – integer value passed to Zstd when compressing data.
* **Cipher algorithm** – string identifying the encryption algorithm. Currently
  only `AES-256-GCM` is supported.

Both values can be provided via a `simplidfs_config.yaml` file or through the
environment variables `SIMPLIDFS_COMPRESSION_LEVEL` and `SIMPLIDFS_CIPHER_ALGO`.
The node and metaserver binaries load these settings on startup and log the
resulting configuration.
