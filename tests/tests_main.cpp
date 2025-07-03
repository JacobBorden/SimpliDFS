#include "utilities/key_manager.hpp"
#include "utilities/logger.h" // Include Logger header
#include "utilities/var_dir.hpp"
#include <filesystem>
#include <gtest/gtest.h>

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  fs::path base = fs::temp_directory_path() / "simplidfs_test_var";
  simplidfs::setVarDir(base.string());
  fs::create_directories(simplidfs::logsDir());
  std::ofstream(simplidfs::fileMetadataPath(), std::ios::app).close();
  std::ofstream(simplidfs::nodeRegistryPath(), std::ios::app).close();

  // Initialize the logger for tests
  try {
    Logger::init(simplidfs::logsDir() + "/simplidfs_tests.log",
                 LogLevel::DEBUG);
    simplidfs::KeyManager::getInstance().initialize();
  } catch (const std::exception &e) {
    std::cerr << "FATAL: Test initialization failed: " << e.what() << std::endl;
    return 1; // Exit if logger or key manager fails to initialize
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
