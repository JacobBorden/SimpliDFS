#!/bin/bash
# Chaos test: deploy edge node, kill it periodically, measure latency
set -euo pipefail

# --- Deploy edge node via Ansible role ---
PLAYBOOK=$(mktemp)
cat > "$PLAYBOOK" <<'PLAY'
- hosts: localhost
  connection: local
  gather_facts: false
  roles:
    - role: edge_node
      vars:
        raft_id: follower1
        raft_peers: leader:50505
        node_name: chaosedge
        node_port: 60020
PLAY

echo "[Chaos] Deploying edge node with Ansible..."
ansible-playbook -i "localhost," "$PLAYBOOK"
rm -f "$PLAYBOOK"

DURATION=$((2*60*60))
INTERVAL=$((5*60))
END=$((SECONDS + DURATION))

while [ $SECONDS -lt $END ]; do
    echo "[Chaos] Stopping simplidfs-node.service"
    systemctl stop simplidfs-node.service || true
    sleep 5
    echo "[Chaos] Starting simplidfs-node.service"
    systemctl start simplidfs-node.service || true

    echo "[Chaos] Running latency benchmark"
    bash "$(dirname "$0")/../bench/run_latency_tests.sh" > /tmp/latency_results.log 2>&1

    METRICS=$(curl -s http://localhost:9100)
    for op in write read; do
        sum=$(echo "$METRICS" | awk -v o="$op" '$1 ~ "simplidfs_fuse_latency_seconds_sum" && $1 ~ o {print $2}')
        count=$(echo "$METRICS" | awk -v o="$op" '$1 ~ "simplidfs_fuse_latency_seconds_count" && $1 ~ o {print $2}')
        if [ -n "$count" ] && [ "$count" != "0" ]; then
            avg=$(awk "BEGIN {print $sum/$count}")
            echo "[Metrics] Avg $op latency: ${avg}s"
        fi
    done
    total=$(echo "$METRICS" | grep '^simplidfs_node_health{' | wc -l)
    alive=$(echo "$METRICS" | grep 'simplidfs_node_health{.*} 1$' | wc -l)
    if [ "$total" -gt 0 ]; then
        sla=$(awk "BEGIN {print 100*$alive/$total}")
        echo "[Metrics] Node availability SLA: ${sla}%"
    fi

    sleep "$INTERVAL"
done
