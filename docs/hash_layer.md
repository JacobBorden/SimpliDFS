# BlockIO SHA-256 Hashing Layer

## 1. Overview of SHA-256 Hashing

The `BlockIO` class has been enhanced to support SHA-256 hashing of all data ingested through its `ingest()` method. This feature allows users to obtain a cryptographic hash of the concatenated data, which can be used to verify data integrity.

The new API for accessing this functionality is:

```cpp
DigestResult BlockIO::finalize_hashed();
```

This method finalizes the data ingestion process and computes the SHA-256 hash. It returns a `DigestResult` struct, defined as follows:

```cpp
#include <array>
#include <vector>
#include <cstddef> // For std::byte
#include <sodium.h>  // For crypto_hash_sha256_BYTES

struct DigestResult {
    std::array<uint8_t, crypto_hash_sha256_BYTES> digest; // SHA-256 hash (32 bytes)
    std::vector<std::byte> raw;                           // Concatenated raw data
};
```

-   `digest`: An array of 32 `uint8_t` values representing the computed SHA-256 digest.
-   `raw`: A vector of `std::byte` containing all the data ingested, in its original order.

Once `finalize_hashed()` is called, the `BlockIO` instance is considered "finalized." Further calls to `ingest()` or `finalize_hashed()` on the same instance will result in a `std::logic_error`.

## 2. Impact of Chunk Size

The SHA-256 algorithm, as implemented using libsodium (`crypto_hash_sha256_update`), processes data incrementally. This means that data is fed into the hashing function chunk by chunk.

**Key Points:**

*   **Total Data Dominates:** The performance of the hashing computation itself is primarily determined by the *total volume* of data processed, not by the size or number of individual chunks passed to the `ingest()` method. Whether you ingest 1 MiB of data in a single call or in 1024 calls of 1 KiB each, the cryptographic operations performed by libsodium will be largely the same.
*   **Function Call Overhead:** While the cryptographic workload remains constant for a given total data size, using extremely small chunks in very frequent calls to `ingest()` can introduce minor overhead. This overhead comes from the repeated function call invocations and the internal state updates within the `BlockIO` class and libsodium's hashing context. However, for typical use cases with reasonably sized chunks (e.g., kilobytes or megabytes), this overhead is generally negligible compared to the time spent on the actual hash calculations.
*   **Buffer Management:** The `BlockIO` class uses an internal `std::vector<std::byte>` to buffer the ingested data. The efficiency of this buffer's management (reallocations, copies) can be influenced by the chunking strategy. If a user frequently ingests very small, unpredictable amounts of data, this might lead to more reallocations of the internal buffer. The hashing update via `crypto_hash_sha256_update` itself is efficient as it operates on the provided data pointers and sizes directly, without necessarily requiring further copies for the hashing context itself.

In summary, while the hashing algorithm is efficient with incremental updates, users should still employ a sensible chunking strategy for `ingest()` calls to optimize overall performance, primarily considering the overhead of buffer management within `BlockIO` and the function call overhead itself if chunks are excessively small and numerous.

## 3. "Invariant Hash First" Principle

A core design principle for the hashing layer in `BlockIO` is **"Invariant Hash First."** This means that the SHA-256 hash is always computed on the data in its original, raw, and unaltered form, exactly as it was provided to the `ingest()` method.

**Importance:**

If `BlockIO` is extended in the future to include additional processing stages—such as data compression or encryption—these transformations would occur *after* the data has been processed by the SHA-256 hashing mechanism (or, more precisely, the hash is calculated on the data *before* it would be passed to such stages).

This ensures that the generated SHA-256 digest always corresponds to the **original plaintext data**. This is crucial for several reasons:

*   **Integrity Verification:** Users can reliably use the hash to verify the integrity of the initial data source, regardless of any subsequent transformations applied for storage or transmission.
*   **Decoupling:** It decouples the integrity check from other data processing operations. You can verify the original data without needing to reverse compression or decryption first.
*   **Clarity:** It provides a clear and unambiguous definition of what the hash represents.

This principle guarantees that the `BlockIO`'s SHA-256 hash serves as a stable and trustworthy fingerprint of the original input.

## 4. Content Identifiers (CIDs)

While raw SHA-256 digests provide excellent data integrity verification, Content Identifiers (CIDs) offer a standardized and more descriptive way to reference content-addressed data.

### What are CIDs?

CIDs are self-describing content-addressed identifiers. This means the identifier itself contains information about how it was generated and what kind of content it refers to, in addition to uniquely identifying the content based on its hash.

### Structure of CIDs Used

The CIDs implemented and used by our system follow a specific structure:

*   **CIDv1**: The latest version of the CID specification, offering better prefix definition and case-insensitivity for base encodings.
*   **Base32 Encoding**: CIDs are typically encoded using Base32 (RFC 4648, with lowercase letters by convention) for efficient and human-readable string representation. This is often prefixed with a multibase prefix (e.g., `b` for Base32).
*   **Multicodec Prefixes**: The core of a CID's self-describing nature comes from multicodec prefixes:
    *   `0x01`: Specifies CIDv1.
    *   `0x70`: Multicodec for `dag-pb`, indicating the content is likely an IPLD DAG Protocol Buffers node. While our raw data might not always be `dag-pb`, this is a common default codec used for general data when not otherwise specified in many IPFS contexts.
    *   `0x12`: Multicodec for `sha2-256`, indicating the hash algorithm used.
    *   `0x20`: Specifies the length of the hash digest in bytes (32 bytes for SHA-256).

A complete CID string thus wraps the raw SHA-256 hash with these defined prefixes, then encodes the entire byte sequence into Base32. For example, `bafy...`.

### Rationale for Adoption

Adopting CIDs provides several key benefits over using raw hash digests:

*   **Self-Describing Hashes**: CIDs embed the hash algorithm (SHA-256), its length, the CID version, and the data codec. This makes identifiers future-proof (e.g., allowing for new hash algorithms) and reduces ambiguity when encountering an identifier.
*   **Interoperability**: CIDs are the standard for addressing data in systems like IPFS, Filecoin, and other decentralized storage networks. Using CIDs improves our ability to interoperate with these ecosystems and leverage associated tools and services.
*   **Standardization**: It aligns our system with a well-defined and widely adopted industry standard for content addressing, moving beyond proprietary or context-specific hash representations.
*   **Uniqueness & Verifiability**: CIDs retain all the benefits of cryptographic hashes. They are still derived from the content's hash, ensuring data integrity, uniqueness, and verifiability.

### Impact on `DigestResult`

Reflecting this enhancement, the `DigestResult` struct returned by `BlockIO::finalize_hashed()` has been updated to include the generated CID:

```cpp
#include <array>
#include <vector>
#include <string>  // For std::string
#include <cstddef> // For std::byte
#include <sodium.h>  // For crypto_hash_sha256_BYTES

struct DigestResult {
    std::array<uint8_t, crypto_hash_sha256_BYTES> digest; // SHA-256 hash
    std::string cid;                                      // Content Identifier (CID) string
    std::vector<std::byte> raw;                           // Concatenated raw data
};
```
The `cid` field now provides a ready-to-use, standard identifier for the hashed content, alongside the raw digest and data.
