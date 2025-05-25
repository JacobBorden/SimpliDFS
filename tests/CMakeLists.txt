cmake_minimum_required(VERSION 3.10)

# Add executable for tests
add_executable(SimpliDFSTests
    tests_main.cpp
    filesystem_tests.cpp
    message_tests.cpp
	metaserver_tests.cpp
    networking_tests.cpp  # Added new test file
    logger_tests.cpp      # Added new logger tests file
    ../src/filesystem.cpp
    ../src/message.cpp
    ../src/client.cpp     # Added client source
    ../src/server.cpp     # Added server source
    ../src/logger.cpp     # Added logger source
    ../src/errorcodes.cpp # Added errorcodes source
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
    ${PROJECT_SOURCE_DIR}/src
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(SimpliDFSTests)