#include <gtest/gtest.h>
#include "utilities/logger.h" // Include Logger header

int main(int argc, char** argv){
    // Initialize the logger for tests
    try {
        Logger::init("simplidfs_tests.log", LogLevel::DEBUG); // Or any desired log level and file
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Logger initialization failed for tests: " << e.what() << std::endl;
        return 1; // Exit if logger fails to initialize
    }

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
