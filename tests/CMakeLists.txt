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
)

# Include directories for the headers
target_include_directories(SimpliDFSTests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(SimpliDFSTests)