cmake_minimum_required(VERSION 3.10)
project(SimpliDFS)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads)

include(FetchContent)

FetchContent_Declare(
	googletest
	GIT_REOPOSITORY https://github.com/google/googletest.git
	SOURCE_DIR ../dependencies/googletest
	)

file(GLOB Filesystem "src/filesystem.h" "src/filesystem.cpp")

enable_testing()
add_subdirectory(tests)
add_subdirectory(dependencies/googletest)
