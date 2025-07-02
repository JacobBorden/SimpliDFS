cmake_minimum_required(VERSION 3.10)
project(SimpliDFS)

# Silence warnings from modern CMake policies when building with newer
# CMake versions.  These policies affect how Boost and FetchContent behave
# but keeping the OLD behavior maintains compatibility with the existing
# build setup.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

option(BUILD_FUZZING "Enable fuzz testing builds" OFF)
option(BUILD_POSIX_TEST_SUITE "Download and build POSIX conformance tests" OFF)

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
set(PKG_CONFIG_USE_STATIC_LIBS ON)
pkg_check_modules(FUSE REQUIRED fuse3) # For FUSE 3.x

include(FetchContent)

# Fetch libsodium at a pinned version to avoid system dependency variation
FetchContent_Declare(
  libsodium
  GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
  GIT_TAG 1.0.19 # Stable tag
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libsodium)

# Build libsodium
execute_process(
  COMMAND ./configure --prefix=${CMAKE_BINARY_DIR}/libsodium_install
  WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
  RESULT_VARIABLE LIBSODIUM_CONFIGURE_RESULT
)
if(NOT LIBSODIUM_CONFIGURE_RESULT EQUAL 0)
  message(FATAL_ERROR "libsodium configure failed")
endif()

find_program(NPROC_COMMAND nproc)
if(NPROC_COMMAND)
  execute_process(
    COMMAND ${NPROC_COMMAND}
    OUTPUT_VARIABLE NUM_PROCESSORS
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
else()
  set(NUM_PROCESSORS 1)
endif()

execute_process(
  COMMAND make -j${NUM_PROCESSORS}
  WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
  RESULT_VARIABLE LIBSODIUM_BUILD_RESULT
)
if(NOT LIBSODIUM_BUILD_RESULT EQUAL 0)
  message(FATAL_ERROR "libsodium build failed")
endif()

execute_process(
  COMMAND make install
  WORKING_DIRECTORY ${libsodium_SOURCE_DIR}
  RESULT_VARIABLE LIBSODIUM_INSTALL_RESULT
)
if(NOT LIBSODIUM_INSTALL_RESULT EQUAL 0)
  message(FATAL_ERROR "libsodium install failed")
endif()

set(SODIUM_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/libsodium_install/include)
set(SODIUM_LIBRARIES ${CMAKE_BINARY_DIR}/libsodium_install/lib/libsodium.a)
# For shared library, it would be:
# set(SODIUM_LIBRARIES ${CMAKE_BINARY_DIR}/libsodium_install/lib/libsodium.so)
# Or link against the interface library if libsodium provided one via CMake.
# Since we are building it manually, we point to the .a file.

# Ensure targets that link against libsodium can find it.
link_directories(${CMAKE_BINARY_DIR}/libsodium_install/lib)
include_directories(${SODIUM_INCLUDE_DIRS}) # Make headers globally available for simplicity here
                                          # More robust would be target_include_directories for specific targets

# Find Boost for the REST server
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS system)

# Fetch zstd
message(STATUS "Fetching zstd from source to ensure consistent version v1.5.5...")
FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG v1.5.5 # Stable tag
  SOURCE_SUBDIR build/cmake # Use the CMakeLists.txt in build/cmake as the top-level one
)
# Set options for zstd build. Respect any values provided on the
# command line so the release workflow can enable static linking.
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Disable zstd programs")
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Disable zstd tests")
option(ZSTD_BUILD_STATIC "Build zstd static library" ON)
option(ZSTD_BUILD_SHARED "Build zstd shared library" OFF)
FetchContent_MakeAvailable(zstd)

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
    GIT_TAG v1.14.0  # Specify the correct branch/tag
    SOURCE_DIR ${CMAKE_BINARY_DIR}/dependencies/googletest
)
# Make GoogleTest available
FetchContent_MakeAvailable(googletest)

# gRPC will provide its own protobuf targets when using the bundled
# version. Avoid calling find_package(Protobuf) to prevent duplicate
# target definitions that lead to configuration errors.

if(WITH_STATIC_LIBS)
  message(STATUS "Using pkg-config to locate gRPC static libraries")
  pkg_check_modules(GRPC REQUIRED grpc++)
  add_library(gRPC_pkg INTERFACE)
  target_include_directories(gRPC_pkg INTERFACE ${GRPC_INCLUDE_DIRS})
  target_link_libraries(gRPC_pkg INTERFACE ${GRPC_LIBRARIES})
  set(GRPC_TARGET gRPC_pkg)
  find_program(GRPC_CPP_PLUGIN NAMES grpc_cpp_plugin REQUIRED)
  # Link statically when requested
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libstdc++ -static-libgcc")
else()
  # Use system-installed gRPC to avoid network downloads
  find_package(gRPC CONFIG REQUIRED)
  set(GRPC_TARGET gRPC::grpc++)
  get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
endif()
set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED)

# Fetch yaml-cpp
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0 # Corrected tag
)
FetchContent_MakeAvailable(yaml-cpp)

# Local third-party libraries
add_subdirectory(third_party/blake3)

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
    PUBLIC # SimpliDFS_Utils headers are needed by users of SimpliDFS_MetaServerLib,
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
add_library(SimpliDFS_NodeLib
    src/node/node.cpp
    src/ipfs_gateway.cpp
)
target_include_directories(SimpliDFS_NodeLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(SimpliDFS_NodeLib
    PUBLIC # If Node.h includes headers from SimpliDFS_Utils that are needed by users of SimpliDFS_NodeLib
    SimpliDFS_Utils
    PRIVATE
    Threads::Threads
)

# ------------------------------------------------------------------------------
# gRPC protobuf generation
# ------------------------------------------------------------------------------

# Directory to place generated sources. Keeping them in the build tree keeps the
# repository clean and ensures regeneration when the proto file changes.
set(GENERATED_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Path to the proto definitions
set(PROTO_FILES "proto/filesystem.proto")

# Ensure the output directory exists so protoc can write files
file(MAKE_DIRECTORY ${GENERATED_PROTO_DIR}/proto)

# Custom command to generate both the protobuf messages and gRPC service stubs.
# Using the protoc executable and grpc_cpp_plugin from the found packages
# guarantees that the headers match the libraries we link against.
add_custom_command(
    OUTPUT
        "${GENERATED_PROTO_DIR}/proto/filesystem.pb.cc"
        "${GENERATED_PROTO_DIR}/proto/filesystem.pb.h"
        "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.cc"
        "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.h"
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --cpp_out=${GENERATED_PROTO_DIR}
            --grpc_out=${GENERATED_PROTO_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating gRPC sources from ${PROTO_FILES}"
    VERBATIM
)


# Expose a custom target so protobuf generation can be explicitly run and so
# downstream targets may depend on it. CMake will only rerun protoc when the
# .proto file changes, keeping rebuilds fast.
add_custom_target(generate_protos
    DEPENDS
        "${GENERATED_PROTO_DIR}/proto/filesystem.pb.cc"
        "${GENERATED_PROTO_DIR}/proto/filesystem.pb.h"
        "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.cc"
        "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.h"
)


# Group generated files for IDE convenience.
set(GENERATED_PROTO_SRCS
    "${GENERATED_PROTO_DIR}/proto/filesystem.pb.cc"
    "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.cc"
)
set(GENERATED_PROTO_HDRS
    "${GENERATED_PROTO_DIR}/proto/filesystem.pb.h"
    "${GENERATED_PROTO_DIR}/proto/filesystem.grpc.pb.h"
)

# Define the gRPC library using the generated sources.
add_library(simplidfs_grpc
    ${GENERATED_PROTO_SRCS}
    src/grpc_server.cpp
)

add_dependencies(simplidfs_grpc generate_protos)



# Include directories for generated headers and public API headers.
target_include_directories(simplidfs_grpc PUBLIC
    "${GENERATED_PROTO_DIR}"
    ${CMAKE_CURRENT_SOURCE_DIR}/include

)

target_link_libraries(simplidfs_grpc
    PUBLIC
    ${GRPC_TARGET}
    SimpliDFS_Utils
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


# S3 Gateway executable
add_library(S3GatewayLib src/s3_gateway.cpp)
target_include_directories(S3GatewayLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(S3GatewayLib PUBLIC SimpliDFS_Utils Threads::Threads)

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
