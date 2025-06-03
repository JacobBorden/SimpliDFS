#!/bin/bash

echo "Starting FUSE test environment cleanup..."

# Kill FUSE adapter
if [ -f /tmp/fuse_adapter.pid ]; then
    FUSE_ADAPTER_PID=$(cat /tmp/fuse_adapter.pid)
    echo "Killing FUSE adapter process $FUSE_ADAPTER_PID..."
    kill $FUSE_ADAPTER_PID
    # Wait for a bit to allow graceful shutdown, then force if necessary
    if ps -p $FUSE_ADAPTER_PID > /dev/null; then
        sleep 2
        if ps -p $FUSE_ADAPTER_PID > /dev/null; then
            echo "FUSE adapter still running, sending SIGKILL..."
            kill -9 $FUSE_ADAPTER_PID
        fi
    fi
    rm -f /tmp/fuse_adapter.pid
else
    echo "FUSE adapter PID file not found."
fi

# Kill Metaserver
if [ -f /tmp/metaserver.pid ]; then
    METASERVER_PID=$(cat /tmp/metaserver.pid)
    echo "Killing Metaserver process $METASERVER_PID..."
    kill $METASERVER_PID
    if ps -p $METASERVER_PID > /dev/null; then
        sleep 2
        if ps -p $METASERVER_PID > /dev/null; then
            echo "Metaserver still running, sending SIGKILL..."
            kill -9 $METASERVER_PID
        fi
    fi
    rm -f /tmp/metaserver.pid
else
    echo "Metaserver PID file not found."
fi

# Unmount FUSE filesystem
echo "Unmounting /tmp/myfusemount..."
# Try fusermount first, then umount as a fallback.
# Redirect errors as they are expected if already unmounted or never mounted.
fusermount -u /tmp/myfusemount > /dev/null 2>&1 || umount /tmp/myfusemount > /dev/null 2>&1
if mount | grep -q /tmp/myfusemount; then
    echo "WARN: /tmp/myfusemount still mounted. Attempting lazy unmount."
    umount -l /tmp/myfusemount > /dev/null 2>&1
    if mount | grep -q /tmp/myfusemount; then
        echo "ERROR: Failed to unmount /tmp/myfusemount even with lazy unmount."
    else
        echo "/tmp/myfusemount unmounted with lazy option."
    fi
else
    echo "/tmp/myfusemount unmounted or was not mounted."
fi


# Remove mount point directory
if [ -d /tmp/myfusemount ]; then
    echo "Removing directory /tmp/myfusemount..."
    rmdir /tmp/myfusemount
    if [ $? -ne 0 ]; then
        echo "WARN: Failed to remove /tmp/myfusemount. It might not be empty or permissions issue."
    else
        echo "Directory /tmp/myfusemount removed."
    fi
else
    echo "Directory /tmp/myfusemount not found."
fi

# Remove log files
echo "Removing log files..."
rm -f /tmp/metaserver.log /tmp/fuse_adapter.log

echo "FUSE test environment cleanup complete."
