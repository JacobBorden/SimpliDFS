cmake_minimum_required(VERSION 3.10)
project(SimpliDFS)

option(BUILD_FUZZING "Enable fuzz testing builds" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Corrected True to ON
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer not to use GNU extensions, stick to standard C++

# Attempt to unify the C++ ABI for std::string
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=1) 
set_property(DIRECTORY PROPERTY COMPILE_DEFINITIONS _GLIBCXX_USE_CXX11_ABI=1)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)
# find_package(FUSE REQUIRED) # Replaced by PkgConfig method
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3) # For FUSE 3.x

include(FetchContent)

# Add libsodium using FetchContent - Now handled by system install and find_package
# FetchContent_Declare(
#     libsodium
#     GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
#     GIT_TAG 1.0.18 # Stable tag
# )
# FetchContent_MakeAvailable(libsodium)

# Find system-installed libsodium
# find_package(Sodium REQUIRED) # Replaced by PkgConfig method

# Use PkgConfig to find libsodium
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# Find Boost for the REST server
find_package(Boost REQUIRED COMPONENTS system)

# Find or fetch zstd
find_package(ZSTD)
if(NOT ZSTD_FOUND)
  message(STATUS "ZSTD not found. Fetching from source...")
  FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.5 # Stable tag
    SOURCE_SUBDIR build/cmake # Use the CMakeLists.txt in build/cmake as the top-level one
  )
  # Set options for zstd build. Using CACHE BOOL without FORCE initially.
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Disable zstd programs")
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Disable zstd tests")
  set(ZSTD_BUILD_STATIC OFF CACHE BOOL "Build zstd static library")
  set(ZSTD_BUILD_SHARED ON CACHE BOOL "Build zstd shared library")
  FetchContent_MakeAvailable(zstd)
else()
  message(STATUS "Found ZSTD: ${ZSTD_LIBRARIES}")
endif()

# Fetch cpp-base32 (cppcodec)
FetchContent_Declare(
  cppcodec
  GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
  GIT_TAG v0.2 # Using specific tag for stability
)
# Disable tests for cppcodec to avoid build issues with its Catch2 version
set(CPPCODEC_BUILD_TESTS OFF CACHE BOOL "Disable cppcodec tests" FORCE)
set(CPPCODEC_BUILD_EXAMPLES OFF CACHE BOOL "Disable cppcodec examples" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable testing for cppcodec subproject" FORCE) # General FetchContent way
FetchContent_MakeAvailable(cppcodec)
set(BUILD_TESTING ON CACHE BOOL "Enable testing for main project" FORCE) # Re-enable for main project

# Corrected FetchContent Declaration for Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main  # Specify the correct branch/tag
    SOURCE_DIR ${CMAKE_BINARY_DIR}/dependencies/googletest
)
# Make GoogleTest available
FetchContent_MakeAvailable(googletest)

message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "_GLIBCXX_USE_CXX11_ABI (Compile Definition Check): $<COMPILE_DEFINITIONS:_GLIBCXX_USE_CXX11_ABI>")


#Fetch Networking Library
# FetchContent_Declare(
# 	networking
# 	GIT_REPOSITORY https://github.com/JacobBorden/NetworkingLibrary.git
# 	GIT_TAG master
# 	SOURCE_DIR ${CMAKE_BINARY_DIR}/dependencies/networking
# )

# FetchContent_MakeAvailable(networking)


# Remove GLOB-based variables for sources, list them explicitly

# Define the SimpliDFS_MetaServerLib library
add_library(SimpliDFS_MetaServerLib
    src/metaserver/metaserver.cpp
    src/metaserver/node_health_tracker.cpp
    src/cluster/NodeHealthCache.cpp
    src/repair/RepairWorker.cpp
    src/repair/ReplicaVerifier.cpp)
target_include_directories(SimpliDFS_MetaServerLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(SimpliDFS_MetaServerLib
    PRIVATE # Should be PUBLIC if SimpliDFS_Utils headers are needed by users of SimpliDFS_MetaServerLib,
            # or INTERFACE if SimpliDFS_Utils is header-only and its headers are included by SimpliDFS_MetaServerLib headers.
            # For now, PRIVATE if SimpliDFS_Utils is mostly .cpp linked into MetaServerLib.
            # If metaserver.h includes headers from SimpliDFS_Utils that then are needed by things linking to MetaServerLib,
            # then SimpliDFS_Utils might need to be PUBLIC on MetaServerLib.
            # Let's assume metaserver.cpp uses SimpliDFS_Utils, and metaserver.h might expose some types from it.
    SimpliDFS_Utils
    Threads::Threads
)

# Define the metaserver executable
add_executable(metaserver src/main_metaserver.cpp) # Assuming main() is moved or is in a separate file
target_link_libraries(metaserver 
    PRIVATE
    SimpliDFS_MetaServerLib
    # SimpliDFS_Utils is already linked via SimpliDFS_MetaServerLib if it's PUBLIC there
    # Threads::Threads is already linked via SimpliDFS_MetaServerLib
)
# Ensure metaserver executable can find headers if main_metaserver.cpp needs them directly
target_include_directories(metaserver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)


# Define the SimpliDFS_NodeLib library
add_library(SimpliDFS_NodeLib src/node/node.cpp)
target_include_directories(SimpliDFS_NodeLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(SimpliDFS_NodeLib
    PUBLIC # If Node.h includes headers from SimpliDFS_Utils that are needed by users of SimpliDFS_NodeLib
    SimpliDFS_Utils
    PRIVATE
    Threads::Threads
)

# Define the node executable
add_executable(node src/main_node.cpp) # Assuming a main_node.cpp for the executable
target_link_libraries(node 
    PRIVATE
    SimpliDFS_NodeLib
    # SimpliDFS_Utils and Threads::Threads are linked via SimpliDFS_NodeLib
)
# Ensure node executable can find headers if main_node.cpp needs them directly
target_include_directories(node PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)


# Control CLI
add_executable(simplidfs src/simplidfs_ctl.cpp)
target_link_libraries(simplidfs PRIVATE SimpliDFS_MetaServerLib)
target_include_directories(simplidfs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)


# Define the FUSE adapter executable
add_executable(simpli_fuse_adapter src/utilities/fuse_adapter.cpp)
target_include_directories(simpli_fuse_adapter
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # For utilities/filesystem.h, utilities/logger.h etc.
    ${FUSE_INCLUDE_DIRS} # Corrected variable
)
target_compile_definitions(simpli_fuse_adapter PRIVATE _FILE_OFFSET_BITS=64) # Add this line
target_link_libraries(simpli_fuse_adapter
    PRIVATE
    SimpliDFS_Utils
    Threads::Threads
    ${FUSE_LIBRARIES}
)

# REST API server
add_executable(rest_server src/rest_server.cpp)
target_include_directories(rest_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(rest_server PRIVATE SimpliDFS_Utils Boost::system Threads::Threads)

# Utilities are now built as a library SimpliDFS_Utils via its own CMakeLists.txt
add_subdirectory(src/utilities)

# Add Google Test subdirectory and enable testing
enable_testing()
add_subdirectory(tests)

# ⭐ NEW CODE
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)
add_executable(health_tests tests/test_health.cpp)
target_link_libraries(health_tests PRIVATE Catch2::Catch2WithMain SimpliDFS_MetaServerLib)
add_test(NAME health_tests COMMAND health_tests)

# Add benchmark subdirectory
add_subdirectory(bench)
