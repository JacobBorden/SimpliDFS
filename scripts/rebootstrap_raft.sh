#!/bin/bash
set -euo pipefail

STATE_DIR="${RAFT_STATE_DIR:-/opt/simplidfs/metadata}"

stop_service() {
    local svc="$1"
    if command -v systemctl >/dev/null 2>&1 && sudo systemctl list-unit-files | grep -q "$svc"; then
        echo "Stopping $svc"
        sudo systemctl stop "$svc" || true
    fi
}

start_service() {
    local svc="$1"
    if command -v systemctl >/dev/null 2>&1 && sudo systemctl list-unit-files | grep -q "$svc"; then
        echo "Starting $svc"
        sudo systemctl start "$svc" || true
    fi
}

# Stop metaservers
stop_service simplidfs-metaserver.service
stop_service simplidfs-metafollower.service

# Remove Raft state files
echo "Clearing Raft state under $STATE_DIR"
rm -f "$STATE_DIR"/raft_* 2>/dev/null || true

# Restart services
start_service simplidfs-metaserver.service
start_service simplidfs-metafollower.service
start_service simplidfs-node.service

echo "Rebootstrap complete."
