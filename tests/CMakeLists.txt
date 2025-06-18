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
    key_manager_tests.cpp
    rbac_tests.cpp
    chunk_store_tests.cpp
    integration_tests.cpp # Added new integration tests file
    s3_gateway_tests.cpp
    raft_tests.cpp
    raft_chaos_tests.cpp
    repair_worker_tests.cpp
    replica_verifier_tests.cpp
    audit_log_tests.cpp
    concurrency_tests.cpp
    node_health_tracker_tests.cpp
    preallocate_file_tests.cpp
    sha256_helper_tests.cpp
    metrics_registry_tests.cpp
    merkle_tree_tests.cpp
    open_file_with_retry_tests.cpp
    node_io_tests.cpp
    stress_utils_tests.cpp
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
    S3GatewayLib
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
add_executable(SimpliDFSFuseRandomWriteTest
    fuse_concurrency_tests.cpp
    fuse_random_write_main.cpp
)
target_link_libraries(SimpliDFSFuseRandomWriteTest
    PRIVATE
    Threads::Threads
    ${SODIUM_LIBRARIES}
)

add_executable(SimpliDFSFuseAppendTest
    fuse_concurrency_tests.cpp
    fuse_append_main.cpp
)
target_link_libraries(SimpliDFSFuseAppendTest
    PRIVATE
    Threads::Threads
    ${SODIUM_LIBRARIES}
)

# Single-thread variants of the FUSE tests. These are compiled with
# FUSE_NUM_THREADS/FUSE_NUM_APPEND_THREADS set to 1 to help diagnose
# concurrency issues by removing multi-threading from the equation.
add_executable(SimpliDFSFuseRandomWriteSingleThreadTest
    fuse_concurrency_tests.cpp
    fuse_random_write_main.cpp
)
target_compile_definitions(SimpliDFSFuseRandomWriteSingleThreadTest PRIVATE FUSE_NUM_THREADS=1)
target_link_libraries(SimpliDFSFuseRandomWriteSingleThreadTest
    PRIVATE
    Threads::Threads
    ${SODIUM_LIBRARIES}
)

add_executable(SimpliDFSFuseAppendSingleThreadTest
    fuse_concurrency_tests.cpp
    fuse_append_main.cpp
)
target_compile_definitions(SimpliDFSFuseAppendSingleThreadTest PRIVATE FUSE_NUM_APPEND_THREADS=1)
target_link_libraries(SimpliDFSFuseAppendSingleThreadTest
    PRIVATE
    Threads::Threads
    ${SODIUM_LIBRARIES}
)

# --- Simple FUSE Tests for debugging ---
add_executable(SimpliDFSFuseMountTest
    fuse_basic_tests.cpp
    fuse_mount_main.cpp
)
target_link_libraries(SimpliDFSFuseMountTest PRIVATE Threads::Threads)

add_executable(SimpliDFSFuseSimpleWriteTest
    fuse_basic_tests.cpp
    fuse_simple_write_main.cpp
)
target_link_libraries(SimpliDFSFuseSimpleWriteTest PRIVATE Threads::Threads)

add_executable(SimpliDFSFuseSimpleAppendTest
    fuse_basic_tests.cpp
    fuse_simple_append_main.cpp
)
target_link_libraries(SimpliDFSFuseSimpleAppendTest PRIVATE Threads::Threads)

add_executable(SimpliDFSFuseStressTest
    fuse_stress_test.cpp
    fuse_stress_main.cpp
)
target_link_libraries(SimpliDFSFuseStressTest PRIVATE Threads::Threads SimpliDFS_Utils)

add_test(
    NAME FuseRandomWriteTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseRandomWriteTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseRandomWriteTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseAppendTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseAppendTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseAppendTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseRandomWriteSingleThreadTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseRandomWriteSingleThreadTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseRandomWriteSingleThreadTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseAppendSingleThreadTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseAppendSingleThreadTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseAppendSingleThreadTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseMountTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseMountTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseMountTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseSimpleWriteTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseSimpleWriteTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseSimpleWriteTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseSimpleAppendTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseSimpleAppendTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseSimpleAppendTest PROPERTIES DEPENDS FuseTestEnvSetup)

add_test(
    NAME FuseStressTest
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_fuse_concurrency_test_wrapper.sh SimpliDFSFuseStressTest
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties(FuseStressTest PROPERTIES DEPENDS FuseTestEnvSetup)
# Define a test for running the setup script
# Define a test for running the setup script
# Fixtures for FuseConcurrencyTest are now handled by the wrapper script.
# Keeping these test definitions if they are useful for standalone execution.
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
# These are removed as the wrapper script now handles setup/cleanup for FuseConcurrencyTest.

# --- End of FUSE Concurrency Test Addition ---

if(BUILD_FUZZING)
    message(STATUS "Fuzz testing build enabled.")

    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "Fuzzing is enabled, but the compiler is not Clang (${CMAKE_CXX_COMPILER_ID}). LibFuzzer typically requires Clang.")
    endif()

    # Common fuzzer compile flags (applied to each fuzzer target)
    set(FUZZER_COMPILE_FLAGS "-fsanitize=fuzzer,address" "-g") # Also add -g for better stack traces
    set(FUZZER_LINK_FLAGS "-fsanitize=fuzzer,address")

    # List of fuzzer source files
    set(FUZZER_SOURCES
        fuzz/blockio_fuzzer.cpp
        fuzz/message_fuzzer.cpp
        fuzz/filesystem_fuzzer.cpp
        fuzz/metaserver_load_fuzzer.cpp
        fuzz/node_logic_fuzzer.cpp
    )

    foreach(FUZZER_SRC ${FUZZER_SOURCES})
        get_filename_component(FUZZER_NAME ${FUZZER_SRC} NAME_WE)
        add_executable(${FUZZER_NAME} ${FUZZER_SRC})

        target_compile_options(${FUZZER_NAME} PRIVATE ${FUZZER_COMPILE_FLAGS})
        # target_link_libraries(${FUZZER_NAME} PRIVATE ${FUZZER_LINK_FLAGS}) # This is more of a linker flag for the main executable in some setups

        # Link necessary project libraries
        if(${FUZZER_NAME} STREQUAL "blockio_fuzzer" OR ${FUZZER_NAME} STREQUAL "message_fuzzer" OR ${FUZZER_NAME} STREQUAL "filesystem_fuzzer" OR ${FUZZER_NAME} STREQUAL "node_logic_fuzzer")
            target_link_libraries(${FUZZER_NAME} PRIVATE SimpliDFS_Utils Threads::Threads ${FUZZER_LINK_FLAGS})
        elseif(${FUZZER_NAME} STREQUAL "metaserver_load_fuzzer")
            target_link_libraries(${FUZZER_NAME} PRIVATE SimpliDFS_MetaServerLib SimpliDFS_Utils Threads::Threads ${FUZZER_LINK_FLAGS})
        else()
            target_link_libraries(${FUZZER_NAME} PRIVATE SimpliDFS_Utils Threads::Threads ${FUZZER_LINK_FLAGS}) # Default, adjust as needed
        endif()

        target_include_directories(${FUZZER_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)

        message(STATUS "Configured fuzzer: ${FUZZER_NAME}")
    endforeach()
endif()

if(BUILD_POSIX_TEST_SUITE)
    add_subdirectory(posix)
endif()
