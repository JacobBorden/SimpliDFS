# Security

## Cluster Encryption Key

SimpliDFS secures data using an AES-256-GCM key shared across the cluster. The
key is loaded from the `SIMPLIDFS_CLUSTER_KEY` environment variable.

### Key Format

`SIMPLIDFS_CLUSTER_KEY` must be a 64-character hexadecimal string (32 bytes).
You can create a suitable value with:

```sh
openssl rand -hex 32
```

### Fallback Behavior

If the environment variable is unset or does not contain a valid hex string,
SimpliDFS generates a random key when the process starts. This key exists only in
memory, so other nodes cannot decrypt data encrypted with it.

Set the environment variable consistently on every node before launching the
metaserver or storage nodes to ensure they can access shared encrypted data.

## FIPS Self Test
SimpliDFS runs a small cryptographic self test at startup. The SHA-256 hash of
"The quick brown fox jumps over the lazy dog" is computed using libsodium and
compared with a known value. The server aborts if the digest does not match.
