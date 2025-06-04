# Testing Guide for SimpliDFS

This guide provides information on running different types of tests included in the SimpliDFS project, with special attention to tests requiring specific runtime environments.

## Standard Unit and Integration Tests (GTest)

Most unit and integration tests are built into the `SimpliDFSTests` executable located in the `build/tests/` directory after compilation. These can be run using CTest from the `build` directory:

```bash
cd build
ctest --output-on-failure -VV
```

For debugging specific GTest cases with GDB, refer to the "Debugging with GDB" section below.

## FUSE-Dependent Tests

Certain tests require a live, mounted FUSE daemon (`simpli_fuse_adapter`) to be running. These tests perform file operations on the specified FUSE mount point to verify the behavior of the distributed filesystem under real-world conditions.

### Identified FUSE-Dependent Tests:

1.  **`FuseConcurrencyTest`**:
    *   **Executable:** `SimpliDFSFuseConcurrencyTest` (found in `build/tests/` after compilation).
    *   **Purpose:** This test evaluates the FUSE daemon's ability to handle concurrent file read/write operations from multiple threads to a single file.
    *   **Required Mount Point:** The test is hardcoded to interact with a FUSE filesystem mounted at `/tmp/myfusemount`. This path is defined by the `MOUNT_POINT` constant in `tests/fuse_concurrency_tests.cpp`.

### Running FUSE-Dependent Tests:

To successfully run tests like `FuseConcurrencyTest`:

1.  **Build in Debug Mode (Recommended for Development):**
    Ensure all components (Metaserver, Node(s), and especially `simpli_fuse_adapter`) are compiled with debug symbols.
    ```bash
    cd <path_to_project_root>/build
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make -j$(nproc)
    ```

2.  **Start Prerequisite Services:**
    *   **Metaserver:** The SimpliDFS Metaserver must be running and accessible.
      ```bash
      # From build/src/metaserver/
      ./simpli_metaserver <port>
      ```
      *(Replace `<port>` with the configured port, e.g., 60000)*

3.  **Start and Mount the FUSE Daemon (`simpli_fuse_adapter`):**
    The `simpli_fuse_adapter` must be running and mounted at the specific path expected by the test. For `FuseConcurrencyTest`, this is `/tmp/myfusemount`.
    *   Ensure the mount point directory exists (e.g., `mkdir -p /tmp/myfusemount`).
    *   Run the FUSE daemon:
      ```bash
      # From build/src/daemon/
      ./simpli_fuse_adapter <metaserver_host> <metaserver_port> /tmp/myfusemount -f -d
      ```
      *(Replace `<metaserver_host>` and `<metaserver_port>` accordingly. Using `-f` (foreground) and `-d` (debug) is highly recommended for observing FUSE daemon logs during testing.)*

4.  **Run the Test(s):**
    Once the Metaserver and FUSE daemon are running and the filesystem is mounted, you can execute the FUSE-dependent tests via CTest from the `build` directory:
    ```bash
    cd build
    ctest --output-on-failure -VV -R FuseConcurrencyTest
    ```
    *(The `-R FuseConcurrencyTest` flag runs only this specific test. Remove it to run all tests, but ensure other tests don't conflict with the FUSE setup if they are not designed for it.)*

5.  **Cleanup:**
    *   After testing, unmount the FUSE filesystem:
      ```bash
      fusermount -u /tmp/myfusemount
      ```
    *   Stop the `simpli_fuse_adapter` (Ctrl+C if in foreground).
    *   Stop the Metaserver.

## Debugging with GDB

### GTest Executable (`SimpliDFSTests`):

1.  **Build in Debug Mode:** (See above)
2.  **Navigate to Test Executable:**
    ```bash
    cd build/tests/
    ```
3.  **List Tests (Optional):**
    ```bash
    ./SimpliDFSTests --gtest_list_tests
    ```
4.  **Run Specific Test under GDB:**
    ```bash
    gdb ./SimpliDFSTests
    ```
    Inside GDB:
    ```gdb
    run --gtest_filter=TestSuiteName.TestName
    ```
    *(Replace `TestSuiteName.TestName` with the actual test name, e.g., `NetworkingTest.ServerShutdownWithActiveClients`)*

### FUSE Concurrency Test (`SimpliDFSFuseConcurrencyTest`):

1.  **Build in Debug Mode:** Both `simpli_fuse_adapter` and `SimpliDFSFuseConcurrencyTest`.
2.  **Terminal 1: Run FUSE Daemon under GDB:**
    ```bash
    cd build/src/daemon/
    gdb ./simpli_fuse_adapter
    ```
    Inside GDB (Terminal 1):
    ```gdb
    run <metaserver_host> <metaserver_port> /tmp/myfusemount -f -d
    ```
3.  **Terminal 2: Run Test Executable under GDB:**
    ```bash
    cd build/tests/
    gdb ./SimpliDFSFuseConcurrencyTest
    ```
    Inside GDB (Terminal 2):
    ```gdb
    run
    ```
4.  **When a Hang Occurs:**
    *   Press `Ctrl+C` in the GDB session of the suspected hung process (either the test executable or the FUSE daemon).
    *   Use GDB commands to inspect:
        *   `bt`: Backtrace of the current thread.
        *   `info threads`: List all threads.
        *   `thread <N>`: Switch to thread N.
        *   `bt` (again): Backtrace for the selected thread.
        *   `p <variable>`: Print variable value.
        *   `f <frame_number>`: Switch to a specific stack frame.
    *   Use `continue`, `next`, `step`, `finish` to control execution.

This structured approach should help in effectively running and debugging all types of tests within the project.
