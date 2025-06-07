# SimpliDFS Architecture Overview

SimpliDFS is a distributed file system designed around a simple metaserver and multiple storage nodes. This document summarizes the key components of the project and how they interact.

## Components

### Metaserver
The metaserver coordinates the cluster, maintains file metadata and orchestrates replication. It keeps track of which nodes store each file and monitors node health. The implementation resides in `src/metaserver` with headers under `include/metaserver`.

### Storage Nodes
Nodes perform the actual data storage. They receive replication commands from the metaserver and expose a gRPC interface for file operations. Source code lives in `src/node` and headers in `include/node`.

### GRPC Services
Communication between the metaserver and nodes uses gRPC. Stubs and service definitions are generated from the `proto` directory. The server setup code can be found in `src/grpc`.

### REST API
A minimal REST server is available for management tasks and testing. It uses Boost.Beast and requires JWT authentication. See `src/rest_server.cpp` and `docs/rest_api.md` for details.

### Cluster Health
Node health information is cached in `NodeHealthCache`. It tracks successes and failures to decide when a node should be marked dead or suspect. The cache is documented in `include/cluster/NodeHealthCache.h`.

### Utilities
Common helpers such as hashing, metrics collection and filesystem utilities are located in `include/utilities` and `src/utilities`.

## Directory Layout

- `include/` - Header files for public components.
- `src/` - Implementation of the metaserver, node logic and utilities.
- `tests/` - Unit and integration tests run via CMake/CTest.
- `docs/` - Design notes (this directory).
- `bench/` - Microbenchmarks for isolated components.
- `scripts/` - Helper scripts for maintenance and development.

## Building and Testing

The recommended build method uses CMake:

```sh
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure -VV -E FuseTestEnv
```

This compiles all binaries and runs the unit tests. Fuse-based integration tests are skipped by default as they require root privileges.

## Generating API Documentation

Running `doxygen docs/Doxyfile` will produce HTML documentation for all headers and source files. The output will be placed under `docs/doxygen/html` and can be viewed in a web browser.
