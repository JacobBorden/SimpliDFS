#include <gtest/gtest.h>
#include <cstdlib>
#include <string>

#ifndef POSIX_SUITE_PATH
#define POSIX_SUITE_PATH ""
#endif

TEST(PosixSuite, PthreadBarrierDestroy)
{
    std::string cmd = std::string("make -C ") + POSIX_SUITE_PATH +
        " conformance/interfaces/pthread_barrier_destroy/1-1.run-test";
    int ret = std::system(cmd.c_str());
    ASSERT_EQ(ret, 0);
}
