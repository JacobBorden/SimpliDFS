#include <gtest/gtest.h>
#include "utilities/logger.h"

int main(int argc, char** argv){

	 // Minimal console logger for the test run
    Logger::init("SimpliDFSTests.log", LogLevel::INFO, /*alsoConsole=*/true);

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
