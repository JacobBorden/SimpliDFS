#!/bin/bash

# Variables
MOUNT_POINT="/mnt/simplidfs"
METASERVER_PORT=60001
METASERVER_BINARY="./build/src/metaserver/simpli_metaserver"
ADAPTER_BINARY="./build/src/utilities/simpli_fuse_adapter" # Corrected path
LOG_FILE="fuse_adapter_run.log"
METASERVER_LOG_FILE="metaserver_for_script.log"
TEST_OUTPUT_FILE="fuse_test_results.log"
METASERVER_PID_FILE="metaserver_script.pid" # PID file for metaserver started by this script

# Cleanup previous run (if any)
echo "INFO: Cleaning up previous run..."
sudo umount "$MOUNT_POINT" 2>/dev/null
# Kill potentially lingering metaserver from a previous run of this script
if [ -f "$METASERVER_PID_FILE" ]; then
    OLD_METASERVER_PID=$(cat "$METASERVER_PID_FILE")
    if ps -p "$OLD_METASERVER_PID" > /dev/null; then
        echo "INFO: Killing lingering metaserver (PID $OLD_METASERVER_PID) from previous run..."
        sudo kill "$OLD_METASERVER_PID"
        sleep 1
        if ps -p "$OLD_METASERVER_PID" > /dev/null; then
            sudo kill -9 "$OLD_METASERVER_PID"
        fi
    fi
    rm -f "$METASERVER_PID_FILE"
fi
rm -f "$LOG_FILE" "$TEST_OUTPUT_FILE" "$METASERVER_LOG_FILE" "fuse_adapter_main.log" # also remove adapter main log

# 1. Create the mount point
echo "INFO: Creating mount point $MOUNT_POINT..."
sudo mkdir -p "$MOUNT_POINT"
if [ ! -d "$MOUNT_POINT" ]; then
    echo "ERROR: Failed to create mount point $MOUNT_POINT"
    exit 1
fi
sudo chown "$(id -u):$(id -g)" "$MOUNT_POINT" # Change ownership to current user
echo "INFO: Mount point created and ownership set."

# 2. Start the Metaserver
echo "INFO: Starting Metaserver on port $METASERVER_PORT..."
"$METASERVER_BINARY" "$METASERVER_PORT" > "$METASERVER_LOG_FILE" 2>&1 &
METASERVER_PID=$!
echo "$METASERVER_PID" > "$METASERVER_PID_FILE"
echo "INFO: Metaserver started with PID $METASERVER_PID. Output logged to $METASERVER_LOG_FILE."
sleep 2 # Give metaserver a moment to initialize

# Check if metaserver started successfully
if ! ps -p $METASERVER_PID > /dev/null; then
    echo "ERROR: Metaserver process $METASERVER_PID failed to start. Check $METASERVER_LOG_FILE."
    cat "$METASERVER_LOG_FILE"
    rm -f "$METASERVER_PID_FILE" # Clean up PID file
    exit 1
fi
echo "INFO: Metaserver process seems to be running."


# 3. Run the compiled FUSE adapter executable in the background
echo "INFO: Starting FUSE adapter: $ADAPTER_BINARY 127.0.0.1 $METASERVER_PORT $MOUNT_POINT -f"
"$ADAPTER_BINARY" "127.0.0.1" "$METASERVER_PORT" "$MOUNT_POINT" -f > "$LOG_FILE" 2>&1 &
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
    # Kill metaserver as well
    echo "INFO: Stopping Metaserver due to FUSE adapter failure..."
    sudo kill $METASERVER_PID
    rm -f "$METASERVER_PID_FILE"
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
    # Kill metaserver as well
    echo "INFO: Stopping Metaserver due to FUSE mount failure..."
    sudo kill $METASERVER_PID
    rm -f "$METASERVER_PID_FILE"
    exit 1
fi
echo "INFO: Mount point $MOUNT_POINT is active."

# 4. Perform list and read operations
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
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: cat $MOUNT_POINT/empty_file.txt ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/empty_file.txt" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: cat $MOUNT_POINT/empty_file.txt failed." | tee -a "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

# --- Specific Tests for touch ---
TOUCH_NEW_FILE="newly_touched_file.txt"
TOUCH_EXISTING_FILE_SETUP="existing_file_for_touch_setup.txt"
TOUCH_NEW_FILE_PATH="$MOUNT_POINT/$TOUCH_NEW_FILE"
TOUCH_EXISTING_FILE_PATH="$MOUNT_POINT/$TOUCH_EXISTING_FILE_SETUP"

echo "--- Test: touch (create) $TOUCH_NEW_FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
touch_new_file_ok=true
touch "$TOUCH_NEW_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then
    echo "ERROR: touch (create) $TOUCH_NEW_FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
    touch_new_file_ok=false
else
    echo "INFO: touch (create) $TOUCH_NEW_FILE_PATH command succeeded." | tee -a "$TEST_OUTPUT_FILE"
    ls -la "$TOUCH_NEW_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: ls -la $TOUCH_NEW_FILE_PATH failed after touch (create)." | tee -a "$TEST_OUTPUT_FILE"
        touch_new_file_ok=false
    else
        echo "INFO: ls -la $TOUCH_NEW_FILE_PATH succeeded." | tee -a "$TEST_OUTPUT_FILE"
    fi

    CREATE_LOG_PATTERN_TOUCH_NEW="simpli_create: Entry for path: /$TOUCH_NEW_FILE" # Check for entry, not specific success message for more robustness
    RELEASE_LOG_PATTERN_TOUCH="simpli_release: Entry for path: /$TOUCH_NEW_FILE"

    if grep -q -E "$CREATE_LOG_PATTERN_TOUCH_NEW" "$LOG_FILE"; then # Check main FUSE log
        echo "INFO: Found simpli_create log entry for $TOUCH_NEW_FILE." | tee -a "$TEST_OUTPUT_FILE"
    else
        echo "ERROR: Did not find simpli_create log entry for $TOUCH_NEW_FILE in $LOG_FILE." | tee -a "$TEST_OUTPUT_FILE"
        touch_new_file_ok=false
    fi
    if grep -q -E "$RELEASE_LOG_PATTERN_TOUCH" "$LOG_FILE"; then # Check main FUSE log
        echo "INFO: Found simpli_release log entry for /$TOUCH_NEW_FILE." | tee -a "$TEST_OUTPUT_FILE"
    else
        echo "ERROR: Did not find simpli_release log entry for /$TOUCH_NEW_FILE in $LOG_FILE." | tee -a "$TEST_OUTPUT_FILE"
        touch_new_file_ok=false
    fi
fi
if [ "$touch_new_file_ok" = true ]; then
    echo "SUCCESS: Test touch (create) $TOUCH_NEW_FILE_PATH passed." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "FAILURE: Test touch (create) $TOUCH_NEW_FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
    echo "DEBUG: Last 50 lines of $LOG_FILE for $TOUCH_NEW_FILE_PATH failure:" | tee -a "$TEST_OUTPUT_FILE"
    tail -n 50 "$LOG_FILE" >> "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"


echo "--- Test: touch (update timestamp) $TOUCH_EXISTING_FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
touch_existing_file_ok=true
echo "Initial content for touch test" > "$TOUCH_EXISTING_FILE_PATH"
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to create setup file $TOUCH_EXISTING_FILE_PATH for touch (update) test." | tee -a "$TEST_OUTPUT_FILE"
    touch_existing_file_ok=false
else
    echo "INFO: Setup file $TOUCH_EXISTING_FILE_PATH created." | tee -a "$TEST_OUTPUT_FILE"
    sleep 1
    touch "$TOUCH_EXISTING_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
    if [ $? -ne 0 ]; then
        echo "ERROR: touch (update) $TOUCH_EXISTING_FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
        touch_existing_file_ok=false
    else
        echo "INFO: touch (update) $TOUCH_EXISTING_FILE_PATH command succeeded." | tee -a "$TEST_OUTPUT_FILE"
        ls -la "$TOUCH_EXISTING_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
        if [ $? -ne 0 ]; then
            echo "ERROR: ls -la $TOUCH_EXISTING_FILE_PATH failed after touch (update)." | tee -a "$TEST_OUTPUT_FILE"
            touch_existing_file_ok=false
        else
            echo "INFO: ls -la $TOUCH_EXISTING_FILE_PATH succeeded." | tee -a "$TEST_OUTPUT_FILE"
        fi
        UTIMENS_LOG_PATTERN="simpli_utimens: Entry for path: /$TOUCH_EXISTING_FILE_SETUP" # Check for entry
        if grep -q -E "$UTIMENS_LOG_PATTERN" "$LOG_FILE"; then # Check main FUSE log
            echo "INFO: Found simpli_utimens log entry for /$TOUCH_EXISTING_FILE_SETUP." | tee -a "$TEST_OUTPUT_FILE"
        else
            echo "ERROR: Did not find simpli_utimens log entry for /$TOUCH_EXISTING_FILE_SETUP in $LOG_FILE." | tee -a "$TEST_OUTPUT_FILE"
            touch_existing_file_ok=false
        fi
    fi
fi
if [ "$touch_existing_file_ok" = true ]; then
    echo "SUCCESS: Test touch (update) $TOUCH_EXISTING_FILE_PATH passed." | tee -a "$TEST_OUTPUT_FILE"
else
    echo "FAILURE: Test touch (update) $TOUCH_EXISTING_FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
    echo "DEBUG: Last 50 lines of $LOG_FILE for $TOUCH_EXISTING_FILE_PATH failure:" | tee -a "$TEST_OUTPUT_FILE"
    tail -n 50 "$LOG_FILE" >> "$TEST_OUTPUT_FILE"
fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Cleanup for touch tests ---" | tee -a "$TEST_OUTPUT_FILE"
rm "$TOUCH_NEW_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then echo "WARN: rm $TOUCH_NEW_FILE_PATH failed during cleanup." | tee -a "$TEST_OUTPUT_FILE"; else echo "INFO: $TOUCH_NEW_FILE_PATH deleted during cleanup." | tee -a "$TEST_OUTPUT_FILE"; fi
rm "$TOUCH_EXISTING_FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then echo "WARN: rm $TOUCH_EXISTING_FILE_PATH failed during cleanup." | tee -a "$TEST_OUTPUT_FILE"; else echo "INFO: $TOUCH_EXISTING_FILE_PATH deleted during cleanup." | tee -a "$TEST_OUTPUT_FILE"; fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: cat $MOUNT_POINT/data.log ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/data.log" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then echo "ERROR: cat $MOUNT_POINT/data.log failed." | tee -a "$TEST_OUTPUT_FILE"; fi
echo "" >> "$TEST_OUTPUT_FILE"

echo "--- Test: cat $MOUNT_POINT/nonexistent.txt ---" | tee -a "$TEST_OUTPUT_FILE"
cat "$MOUNT_POINT/nonexistent.txt" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -eq 0 ]; then echo "ERROR: cat $MOUNT_POINT/nonexistent.txt succeeded but should have failed." | tee -a "$TEST_OUTPUT_FILE"; fi
echo "" >> "$TEST_OUTPUT_FILE"

NEW_FILE="new_test_file.txt"; RENAMED_FILE="renamed_test_file.txt"; TEST_STRING="Hello from new_test_file!"; FILE_PATH="$MOUNT_POINT/$NEW_FILE"; RENAMED_PATH="$MOUNT_POINT/$RENAMED_FILE"
echo "--- Test: Create $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
touch "$FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1
if [ $? -ne 0 ]; then echo "ERROR: touch $FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"; else
    echo "INFO: touch $FILE_PATH command succeeded." | tee -a "$TEST_OUTPUT_FILE"
    ls -la "$FILE_PATH" >> "$TEST_OUTPUT_FILE" 2>&1 || echo "ERROR: ls -la $FILE_PATH failed after touch." | tee -a "$TEST_OUTPUT_FILE"
    CREATE_LOG_PATTERN="simpli_create: Entry for path: /$NEW_FILE"
    if grep -q -E "$CREATE_LOG_PATTERN" "$LOG_FILE"; then echo "INFO: Found simpli_create log entry for $NEW_FILE." | tee -a "$TEST_OUTPUT_FILE"; else
        echo "ERROR: Did not find simpli_create log entry for $NEW_FILE in $LOG_FILE." | tee -a "$TEST_OUTPUT_FILE"; tail -n 50 "$LOG_FILE" >> "$TEST_OUTPUT_FILE"; fi
fi; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Write to $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
echo "$TEST_STRING" > "$FILE_PATH"; if [ ! -s "$FILE_PATH" ]; then echo "WARN: Writing to $FILE_PATH might have failed (file is empty or does not exist)." | tee -a "$TEST_OUTPUT_FILE"; fi
echo "Attempted to write '$TEST_STRING' to $FILE_PATH" >> "$TEST_OUTPUT_FILE"; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Verify content of $FILE_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
CONTENT=$(cat "$FILE_PATH"); echo "Content of $FILE_PATH: '$CONTENT'" >> "$TEST_OUTPUT_FILE"
if [ "$CONTENT" == "$TEST_STRING" ]; then echo "INFO: Content verification for $FILE_PATH successful." | tee -a "$TEST_OUTPUT_FILE"; else echo "ERROR: Content verification for $FILE_PATH failed." | tee -a "$TEST_OUTPUT_FILE"; fi; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Rename $FILE_PATH to $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
mv "$FILE_PATH" "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1 || echo "ERROR: mv $FILE_PATH $RENAMED_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
echo "INFO: $FILE_PATH renamed to $RENAMED_PATH." | tee -a "$TEST_OUTPUT_FILE"; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Verify rename operation ---" | tee -a "$TEST_OUTPUT_FILE"
if [ ! -f "$FILE_PATH" ] && [ -f "$RENAMED_PATH" ]; then echo "INFO: Rename successfully verified." | tee -a "$TEST_OUTPUT_FILE"; ls -la "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1; else
    echo "ERROR: Rename verification failed." | tee -a "$TEST_OUTPUT_FILE"; fi; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Delete $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
rm "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1 || echo "ERROR: rm $RENAMED_PATH failed." | tee -a "$TEST_OUTPUT_FILE"
echo "INFO: $RENAMED_PATH deleted." | tee -a "$TEST_OUTPUT_FILE"; echo "" >> "$TEST_OUTPUT_FILE"
echo "--- Test: Verify deletion of $RENAMED_PATH ---" | tee -a "$TEST_OUTPUT_FILE"
if [ ! -f "$RENAMED_PATH" ]; then echo "INFO: Deletion successfully verified." | tee -a "$TEST_OUTPUT_FILE"; else
    echo "ERROR: Deletion verification failed." | tee -a "$TEST_OUTPUT_FILE"; ls -la "$RENAMED_PATH" >> "$TEST_OUTPUT_FILE" 2>&1; fi; echo "" >> "$TEST_OUTPUT_FILE"

echo "INFO: Tests completed. Results in $TEST_OUTPUT_FILE."
echo "INFO: FUSE adapter log ($LOG_FILE) content:"
cat "$LOG_FILE"
echo "INFO: Metaserver log ($METASERVER_LOG_FILE) content:"
cat "$METASERVER_LOG_FILE"

# 5. Cleanup
echo "INFO: Unmounting $MOUNT_POINT..."
sudo umount "$MOUNT_POINT"
if [ $? -ne 0 ]; then
    echo "WARN: Failed to unmount $MOUNT_POINT cleanly. Attempting lazy unmount."
    sudo umount -l "$MOUNT_POINT" 2>/dev/null
    echo "INFO: Killing FUSE adapter process $ADAPTER_PID due to unmount issues..."
    sudo kill -9 $ADAPTER_PID 2>/dev/null # Force kill if unmount fails
else
    echo "INFO: Unmounted successfully."
    echo "INFO: Stopping FUSE adapter process $ADAPTER_PID..."
    sudo kill $ADAPTER_PID # Graceful kill
fi
sleep 1
if ps -p $ADAPTER_PID > /dev/null; then
    echo "WARN: Adapter process $ADAPTER_PID did not terminate, sending SIGKILL."
    sudo kill -9 $ADAPTER_PID
fi

echo "INFO: Stopping Metaserver process $METASERVER_PID..."
sudo kill $METASERVER_PID
sleep 1
if ps -p $METASERVER_PID > /dev/null; then
    echo "WARN: Metaserver process $METASERVER_PID did not terminate, sending SIGKILL."
    sudo kill -9 $METASERVER_PID
fi
rm -f "$METASERVER_PID_FILE"

# Optional: Remove mount point directory if empty
# sudo rmdir "$MOUNT_POINT" 2>/dev/null

echo "INFO: Test script finished."
echo "INFO: Main adapter log (fuse_adapter_main.log) content:"
if [ -f fuse_adapter_main.log ]; then cat fuse_adapter_main.log; else echo "INFO: fuse_adapter_main.log not found."; fi
echo "INFO: Test results ($TEST_OUTPUT_FILE) content:"
cat "$TEST_OUTPUT_FILE"
