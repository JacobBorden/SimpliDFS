cmake_minimum_required(VERSION 3.10)
project(SimpliDFS)

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

# Create an INTERFACE library for Message (now header-only with inline static methods)
add_library(SimpliDFS_Message INTERFACE)
target_include_directories(SimpliDFS_Message INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src) 

# Define the main executable (SimpliDFS - likely for testing or a simple client)
add_executable(SimpliDFS src/main.cpp src/filesystem.cpp) 
target_include_directories(SimpliDFS PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(SimpliDFS 
    PRIVATE
    SimpliDFS_Message # Still link to propagate include directories if needed
    Threads::Threads
)

# Define the metaserver executable
add_executable(metaserver src/metaserver.cpp src/filesystem.cpp) 
target_include_directories(metaserver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(metaserver 
    PRIVATE
    SimpliDFS_Message 
    Threads::Threads
)

# Define the node executable
add_executable(node src/node.cpp src/filesystem.cpp) 
target_include_directories(node PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(node 
    PRIVATE
    SimpliDFS_Message 
    Threads::Threads
)

# Add Google Test subdirectory and enable testing
enable_testing()
add_subdirectory(tests)


