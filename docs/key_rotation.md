# Cluster Key Rotation

SimpliDFS supports rotating the cluster encryption key while keeping the previous
key available for a short period. Nodes load the key from the
`SIMPLIDFS_CLUSTER_KEY` environment variable or, if unset, from a key that the
`KeyManager` generates automatically at startup. During a rotation, the old key
remains valid briefly so nodes can decrypt data encrypted before the change.

## Steps

1. Ensure all nodes are running the same SimpliDFS version.
2. Build the control utility:

```bash
mkdir -p build && cd build
cmake ..
make simplidfs
```

3. Run `scripts/rotate_key.sh <window>` on one node. `<window>` specifies how
   long, in seconds, the old key remains valid. A typical value is 60 seconds.
4. Restart remaining nodes within the window so they load the new key from the
   running metaserver.
5. After the window expires, the old key is wiped from memory.

