# Documentation Overview

This directory contains design notes and technical references for the SimpliDFS project. Each file focuses on a particular subsystem or deployment concern.

## Available Documents
- `overview.md` - high-level architecture and directory layout.
- `blockio_proto.md` - prototype for a streaming block storage abstraction.
- `fedramp_mapping.md` - mapping of project features to FedRAMP controls.
- `hash_layer.md` - discussion of the hashing layer and use of CIDs.
- `ipfs_gateway.md` - notes on integrating an IPFS gateway.
- `key_rotation.md` - procedures for rotating the cluster encryption key.
- `migration_sketch.md` - strategy for migrating digests to CIDs.
- `pki.md` - public key infrastructure overview.
- `rest_api.md` - REST server usage and endpoints.
- `tiered_storage_playbook.md` - deploying tiered storage across orbit and ground.
- `gcp_ci_cd.md` - deploying and updating the metaserver on Google Cloud.

## API Reference
A `Doxyfile` is provided to generate HTML reference documentation from the source code.
To build it locally (Doxygen 1.9 or later is required):

```sh
doxygen docs/Doxyfile
```

The generated pages will appear under `docs/doxygen/html`.
