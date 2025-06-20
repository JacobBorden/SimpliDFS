# Edge Node Role

This Ansible role bootstraps an Edge Node for SimpliDFS. It installs required packages,
clones the project, builds the binaries and configures two services:

- **simplidfs-metafollower.service** – runs the metadata service in follower mode.
- **simplidfs-node.service** – runs the storage node providing a local data cache.

Variables such as `raft_id`, `raft_peers`, `node_name` and `node_port` can be overridden
from playbooks or inventory.

```
- hosts: edge_nodes
  roles:
    - role: edge_node
      vars:
        raft_id: follower1
        raft_peers: leader:50505
        node_name: edge1
        node_port: 60010
```

The node service depends on the metadata follower and automatically starts after it.

Generated certificates and TPM quotes are stored in `/etc/simplidfs/certs`. The
node service sets `SIMPLIDFS_QUOTE_FILE` so the node can transmit the quote to
the metaserver during registration.
