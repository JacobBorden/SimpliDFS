#!/bin/bash
# Chaos test: randomly kill one node every 5 minutes for 2 hours
set -e
NODES=(Node1 Node2 Node3)
DURATION=$((2*60*60))
INTERVAL=$((5*60))
END=$((SECONDS + DURATION))

while [ $SECONDS -lt $END ]; do
    IDX=$((RANDOM % ${#NODES[@]}))
    NODE_NAME=${NODES[$IDX]}
    PID_FILE="/tmp/${NODE_NAME}.pid"
    if [ -f "$PID_FILE" ]; then
        PID=$(cat "$PID_FILE")
        echo "[Chaos] Killing $NODE_NAME (pid $PID)"
        kill -9 "$PID" 2>/dev/null || true
        rm -f "$PID_FILE"
    else
        echo "[Chaos] $NODE_NAME not running"
    fi
    sleep "$INTERVAL"
done
