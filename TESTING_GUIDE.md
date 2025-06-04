# Testing Guide for SimpliDFS

This guide provides information on running different types of tests included in the SimpliDFS project, with special attention to tests requiring specific runtime environments.

## Standard Unit and Integration Tests (GTest)

Most unit and integration tests are built into the `SimpliDFSTests` executable located in the `build/tests/` directory after compilation. These can be run using CTest from the `build` directory:

```bash
cd build
ctest --output-on-failure -VV
```

For debugging specific GTest cases with GDB, refer to the "Debugging with GDB" section below.


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


This structured approach should help in effectively running and debugging all types of tests within the project.
