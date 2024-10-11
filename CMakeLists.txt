cmake_minimum_required(VERSION 3.10)
project(SimpliDFS)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)

include(FetchContent)

# Corrected FetchContent Declaration for Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG main  # Specify the correct branch/tag
    SOURCE_DIR ${CMAKE_BINARY_DIR}/dependencies/googletest
)

# Make GoogleTest available
FetchContent_MakeAvailable(googletest)

file(GLOB Filesystem "src/filesystem.h" "src/filesystem.cpp")
file(GLOB Message "src/message.h" "src/message.cpp")

# Define the main executable
add_executable(SimpliDFS src/main.cpp ${Filesystem} ${Message}) # Include your main.cpp file

# Link threading library
target_link_libraries(SimpliDFS Threads::Threads)

# Add Google Test subdirectory and enable testing
enable_testing()
add_subdirectory(tests)


