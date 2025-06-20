# Node Attestation

SimpliDFS supports optional TPM-based attestation when storage nodes join a cluster. During certificate generation `scripts/generate_node_cert.sh` will attempt to run `tpm2_getquote` and store the PCR 7 quote in `<node>.quote` next to the node certificate. Development environments without a hardware TPM fall back to a placeholder value.

On startup a node reads the quote file and sends it in the `_Data` field of the `RegisterNode` message. The metaserver compares this value to the expected quote supplied via the `EXPECTED_PCR7_QUOTE` environment variable before accepting the node.

If the quote does not match the expected value, the metaserver rejects the registration with an `EACCES` error.
