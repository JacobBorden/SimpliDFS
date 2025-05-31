#!/bin/bash

# Variables
MOUNT_POINT="/mnt/simplidfs"
ADAPTER_BINARY="./build/simpli_fuse_adapter" # Path from project root
LOG_FILE="fuse_adapter_run.log"
TEST_OUTPUT_FILE="fuse_test_results.log"

# Cleanup previous run (if any)
echo "INFO: Cleaning up previous run..."
sudo umount "$MOUNT_POINT" 2>/dev/null
rm -f "$LOG_FILE" "$TEST_OUTPUT_FILE"

# 1. Create the mount point
echo "INFO: Creating mount point $MOUNT_POINT..."
sudo mkdir -p "$MOUNT_POINT"
if [ ! -d "$MOUNT_POINT" ]; then
    echo "ERROR: Failed to create mount point $MOUNT_POINT"
    exit 1
fi
sudo chown "$(id -u):$(id -g)" "$MOUNT_POINT" # Change ownership to current user
echo "INFO: Mount point created and ownership set."

# 2. Run the compiled FUSE adapter executable in the background
#    We use -f for foreground-like behavior but redirect its output
#    and run it in background to allow this script to continue.
#    The adapter's own logging will go to its stdout/stderr, captured in $LOG_FILE.
echo "INFO: Starting FUSE adapter: $ADAPTER_BINARY $MOUNT_POINT -f"
# fuse_daemon_start_line=$(grep -n "Passing control to fuse_main" src/utilities/fuse_adapter.cpp | cut -d: -f1)
# if [ -z "$fuse_daemon_start_line" ]; then
#     fuse_daemon_start_line="main" # Fallback
# fi
# # Start with gdb for more detailed crash info if needed, but for now, direct run.
# # gdb -batch -ex "run" -ex "bt" --args "$ADAPTER_BINARY" "$MOUNT_POINT" -f > "$LOG_FILE" 2>&1 &
"$ADAPTER_BINARY" "$MOUNT_POINT" -f > "$LOG_FILE" 2>&1 &
ADAPTER_PID=$!
echo "INFO: FUSE adapter started with PID $ADAPTER_PID. Output logged to $LOG_FILE."

# Give FUSE some time to mount and stabilize
echo "INFO: Waiting for FUSE to mount..."
sleep 3 # Increased wait time

# Check if the adapter process is still running
if ! ps -p $ADAPTER_PID > /dev/null; then
    echo "ERROR: FUSE adapter process $ADAPTER_PID died prematurely. Check $LOG_FILE."
    cat "$LOG_FILE" # Display log content for immediate feedback
    # Attempt to unmount just in case it partially mounted
    sudo umount "$MOUNT_POINT" 2>/dev/null
    rmdir "$MOUNT_POINT" 2>/dev/null
    exit 1
fi
echo "INFO: FUSE adapter process seems to be running."

# Check if mount was successful by listing mount point parent
echo "INFO: Checking if mount point is active..."
if ! mount | grep -q "$MOUNT_POINT"; then
    echo "ERROR: $MOUNT_POINT does not appear to be mounted."
    echo "INFO: FUSE adapter log ($LOG_FILE):"
    cat "$LOG_FILE"
    # Kill the adapter process as it failed to mount
    sudo kill $ADAPTER_PID
    sleep 1
    sudo umount "$MOUNT_POINT" 2>/dev/null # Attempt unmount again
    rmdir "$MOUNT_POINT" 2>/dev/null
    exit 1
fi
echo "INFO: Mount point $MOUNT_POINT is active."

# 3. Perform list and read operations
echo "INFO: Performing tests..." | tee -a "$TEST_OUTPUT_FILE"

echo "--- Test: ls -la $MOUNT_POINT ---" | tee -a "$TEST_OUTPUT_FILE"
ls -la "$MOUNT_POINT" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: ls -la $MOUNT_POINT failed." | tee -a "$TEST_OUTPUT_FILE"
fi

echo "--- Test: cat $MOUNT_POINT/hello.txt ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/hello.txt" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: cat $MOUNT_POINT/hello.txt failed." | tee -a "$TEST_OUTPUT_FILE"
fi
# Add a newline to the test output file for cat results for better readability
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: cat $MOUNT_POINT/empty_file.txt ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/empty_file.txt" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: cat $MOUNT_POINT/empty_file.txt failed." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"


echo "--- Test: cat $MOUNT_POINT/data.log ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/data.log" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: cat $MOUNT_POINT/data.log failed." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: cat $MOUNT_POINT/nonexistent.txt ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/nonexistent.txt" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -eq 0 ]; then # Should fail
    echo "ERROR: cat $MOUNT_POINT/nonexistent.txt succeeded but should have failed." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

# --- Create, Write, Unlink, Rename Tests ---

# Variables for new tests
NEW_FILE="new_test_file.txt"
RENAMED_FILE="renamed_test_file.txt"
TEST_STRING="Hello from new_test_file!"
FILE_PATH="$MOUNT_POINT/$NEW_FILE"
RENAMED_PATH="$MOUNT_POINT/$RENAMED_FILE"

echo "--- Test: Create $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
touch "$FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: touch $FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "INFO: $FILE_PATH created." | tee -a "$TEST_OUTPUT_FILE"
fi
ls -la "$FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1 # Verify creation
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Write to $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
echo "$TEST_STRING" > "$FILE_PATH"
# Verify write operation by checking exit status (though echo itself doesn't set $? reliably for this)
# Instead, we will verify by reading back.
if [ ! -s "$FILE_PATH" ]; then # Check if file is not empty
    echo "WARN: Writing to $FILE_PATH might have failed (file is empty or does not exist)." | tee -a "$TEST_OUTPUT_FILE"
fi
# Log the attempt
echo "Attempted to write '$TEST_STRING' to $FILE_PATH" >> "$TEST_OUTPUT_FILE"
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Verify content of $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
CONTENT=$(cat "$FILE_PATH")
echo "Content of $FILE_PATH: '$CONTENT'" >> "$TEST_OUTPUT_FILE"
if [ "$CONTENT" == "$TEST_STRING" ]; then
    echo "INFO: Content verification for $FILE_PATH successful." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "ERROR: Content verification for $FILE_PATH failed. Expected '$TEST_STRING', got '$CONTENT'." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Rename $FILE_PATH to $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
mv "$FILE_PATH" "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: mv $FILE_PATH $RENAMED_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "INFO: $FILE_PATH renamed to $RENAMED_PATH." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Verify rename operation ---" | tee -a "$TEST_OUTPUT_FILE"
if [ ! -f "$FILE_PATH" ] && [ -f "$RENAMED_PATH" ]; then
    echo "INFO: Rename successfully verified. $FILE_PATH does not exist, $RENAMED_PATH exists." | tee -a "$TEST_OUTPUT_FILE"
    ls -la "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
else
    echo "ERROR: Rename verification failed." | tee -a "$TEST_OUTPUT_FILE"
    echo "DEBUG: $FILE_PATH exists? $(test -f "$FILE_PATH" && echo Yes || echo No)" >> "$TEST_OUTPUT_FILE"
    echo "DEBUG: $RENAMED_PATH exists? $(test -f "$RENAMED_PATH" && echo Yes || echo No)" >> "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Delete $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
rm "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: rm $RENAMED_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "INFO: $RENAMED_PATH deleted." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: Verify deletion of $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
if [ ! -f "$RENAMED_PATH" ]; then
    echo "INFO: Deletion successfully verified. $RENAMED_PATH does not exist." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "ERROR: Deletion verification failed. $RENAMED_PATH still exists." | tee -a "$TEST_OUTPUT_FILE"
    ls -la "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
fi
echo "" >> "$TEST_OUTPUT_FILE"

# --- End of Create, Write, Unlink, Rename Tests ---

echo "INFO: Tests completed. Results in $TEST_OUTPUT_FILE."
echo "INFO: FUSE adapter log ($LOG_FILE) content:"
cat "$LOG_FILE"

# 4. Unmount the filesystem
echo "INFO: Unmounting $MOUNT_POINT..."
sudo umount "$MOUNT_POINT"
if [ $? -ne 0 ]; then
    echo "WARN: Failed to unmount $MOUNT_POINT cleanly. Attempting lazy unmount."
    sudo umount -l "$MOUNT_POINT" 2>/dev/null
    # Consider killing the adapter if unmount fails after some time
    echo "INFO: Killing FUSE adapter process $ADAPTER_PID due to unmount issues..."
    sudo kill -9 $ADAPTER_PID 2>/dev/null
else
    echo "INFO: Unmounted successfully."
    # Stop the FUSE adapter process
    echo "INFO: Stopping FUSE adapter process $ADAPTER_PID..."
    sudo kill $ADAPTER_PID
fi

# Wait a moment for the process to terminate
sleep 1
if ps -p $ADAPTER_PID > /dev/null; then
    echo "WARN: Adapter process $ADAPTER_PID did not terminate, sending SIGKILL."
    sudo kill -9 $ADAPTER_PID
fi

# Optional: Remove mount point directory if empty
# sudo rmdir "$MOUNT_POINT" 2>/dev/null

echo "INFO: Test script finished."

echo "INFO: Main adapter log (fuse_adapter_main.log) content:"
if [ -f fuse_adapter_main.log ]; then
    cat fuse_adapter_main.log
else
    echo "INFO: fuse_adapter_main.log not found."
fi

# Display test results file at the end
echo "INFO: Test results ($TEST_OUTPUT_FILE) content:"
cat "$TEST_OUTPUT_FILE"
