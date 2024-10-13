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

#Fetch Networking Library
FetchContent_Declare(
	networking
	GIT_REPOSITORY https://github.com/JacobBorden/NetworkingLibrary.git
	GIT_TAG master
	SOURCE_DIR ${CMAKE_BINARY_DIR}/dependencies/networking
)

FetchContent_MakeAvailable(networking)
# Find the Networking Library
add_subdirectory(${CMAKE_BINARY_DIR}/dependencies/networking)

file(GLOB Filesystem "src/filesystem.h" "src/filesystem.cpp")
file(GLOB Message "src/message.h" "src/message.cpp")
file(GLOB MetaServer "src/metaserver.h" "src/metaserver.cpp")

# Define the main executable
add_executable(SimpliDFS src/main.cpp ${Filesystem} ${Message}) # Include your main.cpp file

# Link threading library
target_link_libraries(SimpliDFS Threads::Threads)

add_executable(metaserver ${MetaServer} ${Filesystem} ${Message})

target_include_directories(metaserver PRIVATE ${CMAKE_BINARY_DIR}/dependencies/networking/src/server)

# Link threading library
target_link_libraries(metaserver Threads::Threads networking)

# Add Google Test subdirectory and enable testing
enable_testing()
add_subdirectory(tests)


