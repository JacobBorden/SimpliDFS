# Cluster Key Rotation

SimpliDFS supports rotating the cluster encryption key while keeping the previous
key available for a short period. This allows running nodes to decrypt data
encrypted with the old key during the transition.

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

