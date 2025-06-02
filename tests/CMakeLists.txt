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
    node_tests.cpp        # Unit tests for Node class logic
    fuse_adapter_tests.cpp # Unit tests for FUSE adapter logic (core functions)
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
)

# Include directories for the headers
target_include_directories(SimpliDFSTests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(SimpliDFSTests)

# Integration Tests
add_executable(SimpliDFSIntegrationTests
    integration_tests.cpp
)

target_link_libraries(SimpliDFSIntegrationTests
    PRIVATE
    gtest_main       # Assuming GTest is setup to provide gtest_main
    Threads::Threads # For std::thread, std::chrono used in integration test fixture
    # SimpliDFS_Utils is not strictly needed if tests only execute external processes
    # and interact via filesystem. Add if direct client/message use is intended.
)

target_include_directories(SimpliDFSIntegrationTests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include # For project headers if needed (e.g. message.h)
)

# Add integration tests to CTest
# Using add_test for more explicit control, could also use gtest_discover_tests
add_test(NAME SimpliDfsSystemIntegrationTests COMMAND SimpliDFSIntegrationTests)