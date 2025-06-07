#include <gtest/gtest.h>
#include "utilities/logger.h" // Include Logger header
#include "utilities/key_manager.hpp"

int main(int argc, char** argv){
    // Initialize the logger for tests
    try {
        Logger::init("simplidfs_tests.log", LogLevel::DEBUG); // Or any desired log level and file
        simplidfs::KeyManager::getInstance().initialize();
    } catch (const std::exception& e) {
        std::cerr << "FATAL: Test initialization failed: " << e.what() << std::endl;
        return 1; // Exit if logger or key manager fails to initialize
    }

	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
