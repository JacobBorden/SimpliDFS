# SimpliDFS PKI Setup

This repository includes a helper script for generating self-signed certificates for development and testing.

```bash
scripts/generate_node_cert.sh <node-name> [output-dir]
```

The script creates a CA if one does not already exist in the output directory and issues a certificate for the specified node. Generated files include `ca.crt`, `ca.key`, `<node-name>.crt`, and `<node-name>.key`.

## Using SPIRE

SimpliDFS can fetch X.509-SVID certificates from a running SPIRE agent. The
agent endpoint is discovered via the `SPIFFE_ENDPOINT_SOCKET` environment
variable and defaults to `/tmp/spire-agent/public/api.sock`.

To start a simple SPIRE test environment using Docker:

```bash
# Start the SPIRE server
docker run -d --name spire-server ghcr.io/spiffe/spire-server:latest \
  /opt/spire/bin/spire-server run -config /opt/spire/conf/server/server.conf

# Start an agent that joins the server
docker run -d --name spire-agent --network container:spire-server \
  -v /tmp/spire-agent:/run/spire ghcr.io/spiffe/spire-agent:latest \
  /opt/spire/bin/spire-agent run -config /opt/spire/conf/agent/agent.conf
```

After creating registration entries, SimpliDFS will automatically obtain SVIDs
when TLS is enabled.
