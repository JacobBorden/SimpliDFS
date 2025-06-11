cmake_minimum_required(VERSION 3.10)
project(BlockIOBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(blockio_bench
    blockio_bench.cpp
    # Add other necessary ../src/*.cpp files if blockio.cpp depends on them
    # For now, blockio.cpp is self-contained with blockio.hpp
)

# gRPC latency benchmark
add_executable(grpc_bench grpc_bench.cpp)
target_link_libraries(grpc_bench PRIVATE
    simplidfs_grpc
    Threads::Threads
)
target_include_directories(grpc_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Ensure headers from src are found
target_include_directories(blockio_bench PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Add ZSTD include directories if zstd is used (similar to SimpliDFS_Utils)
if(ZSTD_INCLUDE_DIRS) # If ZSTD was found by find_package
    target_include_directories(blockio_bench PRIVATE ${ZSTD_INCLUDE_DIRS})
endif()
if(zstd_SOURCE_DIR) # If zstd was fetched by FetchContent
    target_include_directories(blockio_bench PRIVATE ${zstd_SOURCE_DIR}/lib)
endif()

# Link against pthreads if necessary (chrono might require it)
find_package(Threads REQUIRED)
target_link_libraries(blockio_bench PRIVATE SimpliDFS_Utils Threads::Threads)

# Optional: Add optimization flags for release builds if not globally set
# target_compile_options(blockio_bench PRIVATE $<$<CONFIG:Release>:-O3>)
