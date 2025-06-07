# Tiered Storage Deployment Playbook

## 1. Overview

This playbook describes how to deploy SimpliDFS storage nodes across orbit and ground tiers. The orbit tier captures latency-sensitive data close to the source, while the ground tier provides durable bulk storage and long-term retention.

## 2. Orbit Tier Deployment

### 2.1 Node Placement
- Deploy orbit tier nodes on satellites or high-altitude platforms.
- Prioritize low-latency links to sensors or instruments.

### 2.2 Data Flow
- Data is first written to orbit nodes.
- Periodically sync orbit data to ground nodes for persistence.

### 2.3 Storage Backing
- Use flash-based storage to tolerate intermittent connectivity.
- Keep a rolling buffer sized for expected downlink windows.

## 3. Ground Tier Deployment

### 3.1 Node Placement
- Deploy ground tier clusters in reliable data centers.
- Provide redundant power and network connections.

### 3.2 Data Flow
- Receive periodic syncs from orbit tier.
- Serve as the authoritative source for long-term archives.

### 3.3 Storage Backing
- Use high-capacity disks with regular backups.
- Enable replication across geographically separated sites.

## 4. Monitoring

### 4.1 Prometheus Metrics
Existing metrics can be scraped from the metaserver's Prometheus endpoint:
- `simplidfs_node_health` — current health state for each node.
- `simplidfs_raft_role` — role of each node in the Raft cluster.
- `simplidfs_fuse_latency_seconds` — histogram of FUSE call latency.
- `simplidfs_replica_healthy` — indicates if replicas for a file are in sync.
- `simplidfs_tier_bytes{tier="orbit"}` — bytes stored per tier.
- `simplidfs_replication_pending` — count of replicas needing re-replication.

### 4.2 Grafana Dashboard
Import `monitoring/grafana/simplidfs_dashboard.json` into Grafana. The dashboard visualizes node health, Raft roles, FUSE latency, tier storage usage and replication status.

## 5. Ops Sign-Off

This document should be reviewed and approved by the Ops lead prior to distribution.
