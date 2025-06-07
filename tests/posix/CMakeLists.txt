cmake_minimum_required(VERSION 3.10)

include(ExternalProject)

ExternalProject_Add(posix_testsuite
    URL https://codeload.github.com/emscripten-core/posixtestsuite/tar.gz/refs/heads/main
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/posix_testsuite
    CONFIGURE_COMMAND ""
    UPDATE_COMMAND ""
    BUILD_COMMAND make build-tests
    BUILD_IN_SOURCE 1
    PATCH_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> /bin/sh -c "echo '-D_XOPEN_SOURCE=600 -lpthread -lrt -lm' > LDFLAGS"
    INSTALL_COMMAND ""
)

ExternalProject_Get_Property(posix_testsuite source_dir)

add_executable(SimpliDFSPosixTests posix_tests.cpp)
add_dependencies(SimpliDFSPosixTests posix_testsuite)

target_include_directories(SimpliDFSPosixTests PRIVATE ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(SimpliDFSPosixTests PRIVATE gtest_main Threads::Threads)

target_compile_definitions(SimpliDFSPosixTests PRIVATE POSIX_SUITE_PATH=\"${source_dir}\")

add_test(NAME PosixSuite COMMAND SimpliDFSPosixTests WORKING_DIRECTORY ${source_dir})
