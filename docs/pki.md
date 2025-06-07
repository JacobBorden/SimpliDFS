# SimpliDFS PKI Setup

This repository includes a helper script for generating self-signed certificates for development and testing.

```bash
scripts/generate_node_cert.sh <node-name> [output-dir]
```

The script creates a CA if one does not already exist in the output directory and issues a certificate for the specified node. Generated files include `ca.crt`, `ca.key`, `<node-name>.crt`, and `<node-name>.key`.
