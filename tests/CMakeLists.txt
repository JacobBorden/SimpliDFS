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
    ../../src/utilities/filesystem.cpp
    ../../src/utilities/message.cpp
    ../../src/utilities/client.cpp     # Added client source
    ../../src/utilities/server.cpp     # Added server source
    ../../src/utilities/logger.cpp     # Added logger source
    ../../src/utilities/errorcodes.cpp # Added errorcodes source
    ../../src/utilities/blockio.cpp    # Added blockio source
)

# Link GoogleTest, threading library, and other dependencies
target_link_libraries(SimpliDFSTests
    PRIVATE
    gtest_main
    gmock_main
    Threads::Threads
)

# Include directories for the headers
target_include_directories(SimpliDFSTests
    PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(SimpliDFSTests)