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

## Fuzz Testing

Fuzz testing (fuzzing) is a software testing technique that involves providing invalid, unexpected, or random data as input to a program. The goal is to identify vulnerabilities, crashes, or assertion failures that might not be found by traditional unit tests. This project uses libFuzzer, integrated with the Clang compiler, for fuzz testing.

### Building Fuzzers

To build the fuzz targets, you need to have Clang installed and configure the project with the `BUILD_FUZZING` CMake option enabled.

1.  **Prerequisites**:
    *   Clang and LLVM (typically `clang` and `llvm` packages).
    *   Ensure your system has the necessary development tools for C++ compilation.

2.  **Configuration**:
    Navigate to the project's root directory and run CMake with the following options:
    ```bash
    cmake -B build -S . -DBUILD_FUZZING=ON -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    ```
    This command configures the build in the `build` directory, enables fuzzing, and sets Clang as the compiler.

3.  **Compilation**:
    Build the project as usual:
    ```bash
    cmake --build build
    # Alternatively, if you prefer make:
    # cd build && make -j$(nproc)
    ```

4.  **Fuzzer Binaries**:
    The compiled fuzzer executables will be located in the `build/tests/` directory (e.g., `build/tests/message_fuzzer`, `build/tests/blockio_fuzzer`, etc.).

### Running Fuzzers Locally

Once built, you can run individual fuzzers directly. Each fuzzer takes libFuzzer-specific command-line options and optional corpus directories.

1.  **Basic Command Structure**:
    ```bash
    ./build/tests/<fuzzer_name> [libFuzzer_options] [corpus_directory_1] [corpus_directory_2] ...
    ```

2.  **Example**:
    To run the `message_fuzzer` for 60 seconds with a memory limit of 1GB, using an existing corpus and saving new interesting inputs to it:
    ```bash
    # Create a corpus directory if it doesn't exist
    mkdir -p ./corpus/message_fuzzer_corpus
    ./build/tests/message_fuzzer -max_total_time=60 -rss_limit_mb=1024 ./corpus/message_fuzzer_corpus
    ```

3.  **Common libFuzzer Options**:
    *   `-max_total_time=<seconds>`: Run for a maximum of `<seconds>`.
    *   `-rss_limit_mb=<megabytes>`: Limit memory usage.
    *   `-timeout=<seconds>`: Timeout for a single run (useful for detecting hangs).
    *   `-print_final_stats=1`: Print detailed statistics at the end.
    *   `[corpus_directories]`: One or more directories. The fuzzer reads initial inputs from these, and saves new interesting inputs (that expand coverage) back into the first directory.

4.  **Crash Artifacts**:
    If a fuzzer finds a crash, it will save the input that caused the crash into a file in the current working directory (or as specified by `-artifact_prefix`). These files are typically named `crash-<hash>`, `leak-<hash>`, or `timeout-<hash>`.

5.  **Corpus**:
    A corpus is a collection of input files that the fuzzer uses as a starting point. Maintaining a good corpus is crucial for effective fuzzing. The fuzzer will generate new inputs based on the initial corpus and add any coverage-increasing inputs back to the (first) corpus directory.

### GitHub Action for Fuzz Testing

The project includes a GitHub Action workflow defined in `.github/workflows/fuzz_testing.yml` that automates fuzz testing.

*   **Triggers**: The workflow runs automatically on every push to the `main` branch and on every pull request targeting `main`.
*   **Process**:
    1.  Checks out the code.
    2.  Installs Clang.
    3.  Configures CMake with `-DBUILD_FUZZING=ON` and Clang.
    4.  Builds all fuzz targets.
    5.  Runs each discovered fuzzer (executables ending in `_fuzzer` in `build/tests/`) for a short duration (currently ~1 minute per fuzzer).
*   **Artifacts**: If any fuzzer run results in a crash, the workflow uploads the crashing input(s) as build artifacts named `fuzz-artifacts`. These can be downloaded from the "Summary" page of the GitHub Actions run. This helps in reproducing and debugging the issue.

The "Debugging with GDB" section below can be adapted if you need to debug a crash found by a fuzzer. You would typically run the fuzzer executable with the crashing input file as an argument:
```bash
gdb ./build/tests/<fuzzer_name>
(gdb) run <path_to_crashing_input_file>
```

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
