cmake_minimum_required(VERSION 3.10)

# Add executable for tests
add_executable(SimpliDFSTests
    tests_main.cpp
    filesystem_tests.cpp
    message_tests.cpp
	metaserver_tests.cpp
    networking_tests.cpp  # Added new test file
    logger_tests.cpp      # Added new logger tests file
    blockio_test.cpp      # Added blockio tests
    cid_tests.cpp         # Added CID conversion tests
    integration_tests.cpp # Added new integration tests file
    # Removed direct compilation of utility sources:
    # ../../src/utilities/filesystem.cpp
    # ../../src/utilities/message.cpp
    # ../../src/utilities/client.cpp
    # ../../src/utilities/server.cpp
    # ../../src/utilities/logger.cpp
    # ../../src/utilities/errorcodes.cpp
    # ../../src/utilities/blockio.cpp
)

# Link GoogleTest, threading library, and other dependencies
target_link_libraries(SimpliDFSTests
    PRIVATE
    gtest_main
    gmock_main
    Threads::Threads
    SimpliDFS_Utils # Link against common utilities library (now includes BlockIO)
    SimpliDFS_MetaServerLib # Link against the new metaserver library
    SimpliDFS_NodeLib       # Link against the new node library
)

# Include directories for the headers
target_include_directories(SimpliDFSTests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(SimpliDFSTests)

# --- Add FUSE Concurrency Test ---
# Add executable for the FUSE concurrency test
add_executable(SimpliDFSFuseConcurrencyTest
    fuse_concurrency_tests.cpp
)

# Link necessary libraries (primarily pthreads for std::thread)
# Threads::Threads is the modern CMake way to handle threading libraries.
target_link_libraries(SimpliDFSFuseConcurrencyTest
    PRIVATE
    Threads::Threads
)

# Add the FUSE concurrency test to CTest
add_test(
    NAME FuseConcurrencyTest
    COMMAND SimpliDFSFuseConcurrencyTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} # Or specific path if needed
)

# Define a test for running the setup script
add_test(
    NAME FuseTestEnvSetup
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/setup_fuse_test_env.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} # Scripts expect to be run from build/tests
)

# Define a test for running the cleanup script
add_test(
    NAME FuseTestEnvCleanup
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cleanup_fuse_test_env.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} # Scripts expect to be run from build/tests
)

# Set test properties for fixtures
set_property(TEST FuseConcurrencyTest PROPERTY FIXTURES_SETUP FuseTestEnvSetup)
set_property(TEST FuseConcurrencyTest PROPERTY FIXTURES_CLEANUP FuseTestEnvCleanup)
set_property(TEST FuseConcurrencyTest PROPERTY DEPENDS FuseTestEnvSetup)

# --- End of FUSE Concurrency Test Addition ---
