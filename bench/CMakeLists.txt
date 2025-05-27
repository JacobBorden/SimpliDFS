cmake_minimum_required(VERSION 3.10)
project(BlockIOBench)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(blockio_bench
    blockio_bench.cpp
    ../src/blockio.cpp # Include the source directly
    # Add other necessary ../src/*.cpp files if blockio.cpp depends on them
    # For now, blockio.cpp is self-contained with blockio.hpp
)

# Ensure headers from src are found
target_include_directories(blockio_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# Link against pthreads if necessary (chrono might require it)
find_package(Threads REQUIRED)
target_link_libraries(blockio_bench PRIVATE Threads::Threads)

# Optional: Add optimization flags for release builds if not globally set
# target_compile_options(blockio_bench PRIVATE $<$<CONFIG:Release>:-O3>)
