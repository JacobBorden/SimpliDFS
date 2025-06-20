#Node Attestation

SimpliDFS verifies new nodes using TPM quotes. The `scripts/generate_node_cert.sh` helper now
produces a `*.quote` file alongside the certificate when invoked. In production
a hardware TPM is queried via `tpm2_getquote` for PCR 7. Development
installations fall back to swtpm or a placeholder string.

During registration each node reads its quote file and sends the contents in the
`RegisterNode` message. The metaserver hashes the quote with SHA-256 and compares
it to the value configured in the `SIMPLIDFS_EXPECTED_PCR7` environment
variable. Registration fails with `EPERM` if the digest does not match. If the
variable is unset the check is skipped, which is suitable for local testing.
