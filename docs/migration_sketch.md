# Migration Plan: From SHA-256 Digests to CIDs

## 1. Introduction

This document outlines a sketch for migrating our system's metadata from using raw SHA-256 digests to Content Identifiers (CIDs). The goal is to provide a structured approach to this transition, ensuring data integrity, minimizing disruption, and leveraging the benefits of CIDs.

## 2. Current State

Currently, our system primarily uses raw SHA-256 digests (typically 32-byte binary strings or their hex-encoded representation) as unique identifiers for data blocks, files, or other digital assets. This information is stored in various metadata repositories (e.g., databases, configuration files, object storage metadata).

While effective for ensuring data integrity via checksums, raw digests lack inherent information about the hashing algorithm used or the data encoding format they represent.

## 3. Target State

The desired state is to utilize CIDs (specifically CIDv1) as the primary identifiers in our metadata. CIDs will encapsulate the SHA-256 digest while also including multicodec prefixes that specify the hashing algorithm (SHA2-256), the version of the CID, and the encoding of the underlying data (e.g., `dag-pb` for IPLD data structures).

This means metadata entries that previously stored a raw SHA-256 digest will store a string-represented CID (e.g., "bafy...").

## 4. Why Migrate to CIDs?

Migrating to CIDs offers several advantages:

*   **Self-Describing**: CIDs embed information about the hashing algorithm, CID version, and data codec, making identifiers future-proof and less ambiguous.
*   **Interoperability**: CIDs are a standard in decentralized systems (IPFS, Filecoin, etc.) and various content-addressing applications, improving interoperability with other tools and platforms.
*   **Standard Compliance**: Adopting CIDs aligns our system with widely accepted industry standards for content identification.
*   **Flexibility**: The multiformats ecosystem (multihash, multicodec, multiaddr, multibase) allows for future evolution (e.g., adopting new hash algorithms) without breaking the identification scheme.

## 5. Migration Strategies

Several strategies can be employed for this migration. The choice depends on factors like data volume, system availability requirements, and application complexity.

### Option 1: Dual Storage / Gradual Migration

*   **Description**: During a transition period, metadata records store both the old raw SHA-256 digest and the new CID.
*   **Process**:
    1.  Modify metadata schemas to include a new field for CIDs (e.g., `cid_v1_sha256`).
    2.  New data written to the system generates and stores CIDs exclusively. The raw digest might still be stored temporarily or derived on-the-fly if needed by older components.
    3.  A background process (or a script run during low-traffic periods) iterates through existing metadata, calculates CIDs from raw digests, and populates the new CID field.
    4.  Applications are updated incrementally:
        *   Newer versions are programmed to prefer reading/using the CID.
        *   If the CID field is empty (for not-yet-migrated data), they fall back to using the raw digest (and can optionally trigger its conversion to CID).
    5.  Once all metadata is updated and applications are migrated, the old raw digest field can be deprecated and eventually removed.
*   **Pros**: Minimal downtime, allows for phased rollout, lower risk as fallback is available.
*   **Cons**: Increased storage temporarily, more complex application logic during transition.

### Option 2: On-the-fly Conversion

*   **Description**: Convert raw digests to CIDs in memory when metadata is accessed.
*   **Process**:
    1.  Applications are updated to understand that a metadata field might contain an old raw digest.
    2.  When such a field is read, the application converts the digest to a CID in memory using a utility function (like `digest_to_cid`).
    3.  If the metadata is mutable and subsequently updated, the application writes back the identifier as a CID, effectively converting that piece of metadata.
    4.  A background process might still be beneficial to ensure all data is eventually converted, even if not accessed and rewritten.
*   **Pros**: Potentially simpler than dual storage if write-backs are frequent or data volume is small; no schema changes immediately required if the existing field can accommodate CIDs (e.g. if it's a string field that previously stored hex digests).
*   **Cons**: Performance overhead for repeated conversions if the same old data is accessed frequently; migration completeness depends on data access patterns or requires a supplemental background job.

### Option 3: Batch Conversion / Downtime

*   **Description**: A scheduled maintenance window is used to convert all existing digests to CIDs in one go.
*   **Process**:
    1.  Schedule system downtime.
    2.  Take the system offline or into read-only mode.
    3.  Run a pre-tested script/tool that:
        *   Reads all metadata entries.
        *   Converts raw SHA-256 digests to CIDs.
        *   Updates the metadata store with the new CIDs (either replacing the old digest or populating a new field and then renaming/dropping the old one).
    4.  Deploy updated applications that exclusively use CIDs.
    5.  Bring the system back online.
*   **Pros**: Simpler application logic post-migration (no need to handle dual formats); ensures all data is converted consistently.
*   **Cons**: Requires system downtime; higher risk if the batch process fails or takes longer than expected.

## 6. Key Considerations

*   **Data Volume**: How many metadata records need conversion? Large volumes can impact the duration of batch processes or the storage overhead of dual storage.
*   **System Availability**: What are the uptime requirements? Can the system afford downtime for a batch conversion?
*   **Application Impact**:
    *   How many applications read/write this metadata?
    *   How complex are the updates required for these applications to support CIDs?
    *   Can applications be updated incrementally?
*   **Rollback Plan**:
    *   How can the migration be rolled back if critical issues arise?
    *   For dual storage, rollback might mean clearing the CID field.
    *   For batch conversion, a backup taken before the migration is essential.
*   **Verification**:
    *   How will the correctness of the migration be verified? (e.g., spot checks, checksums of metadata tables, comparison scripts).
    *   Test conversion logic thoroughly (as done with the fuzz tests for `cid_utils`).
*   **Performance**: Consider the performance impact of on-the-fly conversions or background jobs.
*   **Data Schema Evolution**: How will database schemas or data structures be modified to accommodate CIDs (which are strings, potentially longer than hex-encoded digests)?

## 7. Recommended Steps (General)

1.  **Analysis & Planning**:
    *   Inventory all systems and datastores using SHA-256 digests.
    *   Estimate data volume and assess application dependencies.
    *   Define acceptable downtime, if any.
    *   Choose the most suitable migration strategy (or a hybrid).
2.  **Tooling & Preparation**:
    *   Develop and rigorously test conversion scripts/libraries (e.g., `cid_utils.hpp/.cpp`).
    *   Prepare a detailed rollback plan.
    *   Create backups of all relevant metadata before any changes.
3.  **Application Updates**:
    *   Update applications to be CID-aware (either to handle dual formats or CIDs exclusively, depending on the strategy).
    *   Test updated applications thoroughly in a staging environment.
4.  **Execution**:
    *   Execute the chosen migration plan (e.g., enable dual storage, start background jobs, or perform batch conversion during a maintenance window).
    *   Monitor the process closely.
5.  **Verification**:
    *   After migration (or a significant phase), verify data integrity and correctness.
    *   Compare a sample of converted CIDs against their original digests.
    *   Check for any errors or inconsistencies.
6.  **Monitoring & Cleanup**:
    *   Monitor system performance and error logs post-migration.
    *   If using dual storage, plan for the eventual removal of the old raw digest fields after a suitable confidence period.

## 8. Conclusion

Migrating from raw SHA-256 digests to CIDs is a strategic improvement that enhances the robustness, interoperability, and future-readiness of our systems. A well-planned migration, considering the specific characteristics of our data and applications, is crucial for a successful transition. This document provides a foundational sketch; further detailed planning will be required for each affected component.
