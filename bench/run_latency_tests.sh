#!/bin/bash

# Script to perform basic latency tests on FUSE filesystem and ext4 baseline.

# --- Configuration ---
FUSE_MOUNT_POINT="/tmp/myfusemount" # Target FUSE mount point
EXT4_TEST_DIR="/tmp/ext4_latency_test_dir" # Directory on a native ext4 filesystem
TEST_FILE_PREFIX="latency_test_file"
FILE_SIZE_KB=10240 # 10MB file for write/read tests (dd bs=1k count=10240)
SMALL_FILE_COUNT=1000 # Number of small files for create/delete tests

# ANSI Color Codes
GREEN='[0;32m'
YELLOW='[0;33m'
RED='[0;31m'
NC='[0m' # No Color

# --- Helper Functions ---
ensure_dir() {
    if [ ! -d "$1" ]; then
        echo -e "${YELLOW}Creating directory: $1${NC}"
        mkdir -p "$1"
        if [ $? -ne 0 ]; then
            echo -e "${RED}Failed to create directory $1. Aborting.${NC}"
            exit 1
        fi
    fi
}

cleanup_dir() {
    if [ -d "$1" ]; then
        echo -e "${YELLOW}Cleaning up directory: $1${NC}"
        # Use find to delete only test files if other files might exist
        find "$1" -name "${TEST_FILE_PREFIX}*" -type f -delete
        # Or rm -rf "$1"/* if the directory should only contain test files
    else
        echo -e "${YELLOW}Directory $1 does not exist, no cleanup needed.${NC}"
    fi
}

# $1: Operation description (e.g., "Write (dd)")
# $2: Filesystem type (e.g., "FUSE", "ext4")
# $3: Command to time (must be quoted if it contains spaces)
# $4: Output file for results (optional)
run_and_time() {
    local description="$1"
    local fs_type="$2"
    local cmd_to_run="$3"
    local results_file="$4" # Not used in this simplified version, but could be for CSV

    echo -e "
${GREEN}Testing: $description on $fs_type${NC}"
    echo "Command: $cmd_to_run"

    # Using /usr/bin/time for more detailed output if available, otherwise default time
    # The -p option for POSIX standard output format (real, user, sys)
    if command -v /usr/bin/time &> /dev/null; then
        TIME_CMD="/usr/bin/time -p"
    else
        TIME_CMD="time" # Shell built-in time
    fi

    # Execute the command and capture time output
    # stderr is redirected to stdout for `time` output, then stdout captured by variable
    # This is tricky with `time` as it often writes to stderr.
    # A more robust way is to redirect stderr of the timed command to a temp file.

    local time_output
    # Execute command, redirect its stdout/stderr to /dev/null to not clutter timing.
    # Time output goes to its stderr, capture that.
    time_output=$({ $TIME_CMD $cmd_to_run; } 2>&1 >/dev/null)

    local real_time=$(echo "$time_output" | grep '^real' | awk '{print $2}')
    local user_time=$(echo "$time_output" | grep '^user' | awk '{print $2}')
    local sys_time=$(echo "$time_output" | grep '^sys' | awk '{print $2}')

    echo "Result for $description on $fs_type:"
    echo "  Real time: ${real_time}s"
    echo "  User time: ${user_time}s"
    echo "  Sys time : ${sys_time}s"

    # Simple check for command success based on exit code
    # Note: $? after command substitution reflects the last command in pipeline (echo)
    # Need to capture $? from $cmd_to_run itself if critical.
    # For simplicity, this script assumes commands will pass or dd/rm will show errors.
}


# --- Main Script ---

echo -e "${GREEN}Starting Latency Benchmark Script${NC}"

# 1. Setup: Ensure directories exist
ensure_dir "$FUSE_MOUNT_POINT"
ensure_dir "$EXT4_TEST_DIR"

# Check if FUSE mount point is actually mounted (basic check)
if ! mountpoint -q "$FUSE_MOUNT_POINT"; then
    echo -e "${RED}Error: $FUSE_MOUNT_POINT does not appear to be a mount point.${NC}"
    echo -e "${YELLOW}Please ensure your FUSE filesystem is mounted there before running.${NC}"
    # exit 1 # Commented out to allow testing ext4 part even if FUSE not ready
fi


# --- Test Scenarios ---

for fs_type in "ext4" "FUSE"; do
    current_test_dir=""
    if [ "$fs_type" == "ext4" ]; then
        current_test_dir="$EXT4_TEST_DIR"
    elif [ "$fs_type" == "FUSE" ]; then
        current_test_dir="$FUSE_MOUNT_POINT"
    else
        echo -e "${RED}Unknown filesystem type: $fs_type${NC}"
        continue
    fi

    echo -e "
${YELLOW}--- Benchmarking on $fs_type ($current_test_dir) ---${NC}"
    cleanup_dir "$current_test_dir" # Clean before tests

    # Test 1: Large file write (using dd)
    # Using oflag=dsync for more direct writing, bypass some OS caching.
    # For a true sync test, 'conv=fdatasync' or 'oflag=sync' might be used,
    # but dsync is a good compromise.
    test_file_path_dd="$current_test_dir/${TEST_FILE_PREFIX}_dd.dat"
    cmd_dd_write="dd if=/dev/zero of=\"$test_file_path_dd\" bs=1k count=$((FILE_SIZE_KB)) oflag=dsync status=none"
    run_and_time "Large File Write (${FILE_SIZE_KB}KB, dd)" "$fs_type" "$cmd_dd_write"

    # Test 2: Large file read (using dd)
    # Using iflag=direct can bypass OS cache for reads on some systems/dd versions.
    cmd_dd_read="dd if=\"$test_file_path_dd\" of=/dev/null bs=1k iflag=direct status=none"
    # Fallback if iflag=direct is not supported or causes issues
    # cmd_dd_read="dd if=\"$test_file_path_dd\" of=/dev/null bs=1k status=none"
    run_and_time "Large File Read (${FILE_SIZE_KB}KB, dd)" "$fs_type" "$cmd_dd_read"

    # Test 3: Create many small files (using touch)
    # This test focuses on metadata operation speed (file creation).
    # The loop is part of the timed command.
    cmd_create_small="for i in \$(seq 1 $SMALL_FILE_COUNT); do touch \"$current_test_dir/${TEST_FILE_PREFIX}_small_\$i.txt\"; done"
    run_and_time "Create $SMALL_FILE_COUNT Small Files (touch)" "$fs_type" "$cmd_create_small"

    # Test 4: Delete many small files (using find ... -delete or rm)
    # Using find...-delete is often efficient.
    cmd_delete_small_find="find \"$current_test_dir\" -name \"${TEST_FILE_PREFIX}_small_*.txt\" -type f -delete"
    # Alternative using rm inside a loop (might be slower due to more command invocations)
    # cmd_delete_small_rm="for i in \$(seq 1 $SMALL_FILE_COUNT); do rm -f \"$current_test_dir/${TEST_FILE_PREFIX}_small_\$i.txt\"; done"
    run_and_time "Delete $SMALL_FILE_COUNT Small Files (find)" "$fs_type" "$cmd_delete_small_find"

    # Test 5: Sequential single byte writes (less common but can stress some aspects)
    # test_file_path_seq_write="$current_test_dir/${TEST_FILE_PREFIX}_seq_write.dat"
    # cmd_seq_write="bash -c 'for i in \$(seq 1 1024); do echo -n "A" >> \"$test_file_path_seq_write\"; sync; done'"
    # run_and_time "Sequential Single Byte Writes (1024 bytes with sync)" "$fs_type" "$cmd_seq_write"
    # This test is commented out as it's very slow and results can be tricky to interpret without careful sync.

    cleanup_dir "$current_test_dir" # Clean up after tests for this fs_type
done

echo -e "
${GREEN}Latency Benchmark Script Finished.${NC}"
echo -e "${YELLOW}NOTE: Results are basic. For rigorous benchmarking, use tools like fio, iozone, or bonnie++. Ensure tests run multiple times for averages.${NC}"
echo -e "${YELLOW}Consider system load and caching effects when interpreting results.${NC}"

# Make the script executable: chmod +x bench/run_latency_tests.sh
