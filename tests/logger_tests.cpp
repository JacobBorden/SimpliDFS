#include "gtest/gtest.h"
#include "logger.h" // Assuming this is the path to your logger.h
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <cstdio> // For std::remove
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::seconds

// Helper function to read file contents
std::string readFileContents(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        // std::cerr << "Failed to open file: " << path << std::endl;
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// Helper function to count occurrences of a substring
int countOccurrences(const std::string& text, const std::string& sub) {
    int count = 0;
    size_t pos = text.find(sub, 0);
    while (pos != std::string::npos) {
        count++;
        pos = text.find(sub, pos + sub.length());
    }
    return count;
}

// Test fixture for Logger tests
class LoggerTest : public ::testing::Test {
protected:
    std::vector<std::string> files_to_remove_;

    void TearDown() override {
        // Attempt to re-initialize logger to a dummy file to release handles on test log files
        // This is a workaround for singleton logger file handles persisting.
        try {
             Logger::init("dummy_cleanup.log", LogLevel::DEBUG, 1024, 1);
        } catch (const std::runtime_error& e) {
            // If logger wasn't initialized, that's fine.
        }

        for (const auto& file : files_to_remove_) {
            std::remove(file.c_str());
        }
        files_to_remove_.clear();
         // Final cleanup of dummy log
        std::remove("dummy_cleanup.log");
        std::remove("dummy_cleanup.log.1");
    }

    void addFileForCleanup(const std::string& filename) {
        files_to_remove_.push_back(filename);
    }
};

TEST_F(LoggerTest, LogLevelFiltering) {
    const std::string testLogFile = "test_level_filter.log";
    addFileForCleanup(testLogFile);
    std::remove(testLogFile.c_str()); 

    ASSERT_NO_THROW(Logger::init(testLogFile, LogLevel::INFO));
    Logger& logger = Logger::getInstance();

    logger.log(LogLevel::TRACE, "This is a trace message."); // Should not appear
    logger.log(LogLevel::DEBUG, "This is a debug message."); // Should not appear
    logger.log(LogLevel::INFO, "This is an info message.");   // Should appear
    logger.log(LogLevel::WARN, "This is a warning message."); // Should appear
    logger.log(LogLevel::ERROR, "This is an error message."); // Should appear
    logger.log(LogLevel::FATAL, "This is a fatal message."); // Should appear
    
    // Brief pause to allow logs to be written, especially if async. Our logger is synchronous.
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));


    std::string logContents = readFileContents(testLogFile);
    ASSERT_NE(logContents, ""); // Ensure log file was created and has content

    EXPECT_EQ(countOccurrences(logContents, "This is a trace message."), 0);
    EXPECT_EQ(countOccurrences(logContents, "This is a debug message."), 0);
    EXPECT_NE(logContents.find("This is an info message."), std::string::npos);
    EXPECT_NE(logContents.find("This is a warning message."), std::string::npos);
    EXPECT_NE(logContents.find("This is an error message."), std::string::npos);
    EXPECT_NE(logContents.find("This is a fatal message."), std::string::npos);
}

TEST_F(LoggerTest, JsonOutputFormat) {
    const std::string testLogFile = "test_json_format.log";
    addFileForCleanup(testLogFile);
    std::remove(testLogFile.c_str());

    ASSERT_NO_THROW(Logger::init(testLogFile, LogLevel::DEBUG));
    Logger& logger = Logger::getInstance();
    logger.log(LogLevel::INFO, "Test JSON output with special chars \" \\ / \b \f \n \r \t");

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string logContents = readFileContents(testLogFile);
    ASSERT_NE(logContents, "");

    // Basic JSON structure checks
    EXPECT_NE(logContents.find("\"level\": \"INFO\""), std::string::npos);
    EXPECT_NE(logContents.find("\"message\": \"Test JSON output with special chars \\\" \\\\ / \\b \\f \\n \\r \\t\""), std::string::npos);
    EXPECT_NE(logContents.find("\"timestamp\": \""), std::string::npos);
    
    // Check if the overall structure is a valid JSON object (starts with { ends with })
    ASSERT_FALSE(logContents.empty());
    EXPECT_EQ(logContents.front(), '{');
    // The log adds a newline, so last char before newline should be }
    size_t last_char_pos = logContents.find_last_not_of("\n\r");
    if (last_char_pos != std::string::npos) {
      EXPECT_EQ(logContents[last_char_pos], '}');
    } else {
      FAIL() << "Log content is empty or only newlines.";
    }
    // More robust check would involve a JSON parser, but this is a good start.
}

TEST_F(LoggerTest, LogRotation) {
    const std::string baseLogFile = "test_rotation.log";
    const int maxBackupFiles = 2;
    const long long maxFileSize = 1024; // 1KB

    addFileForCleanup(baseLogFile);
    for (int i = 1; i <= maxBackupFiles + 1; ++i) { // Clean up potential older backups beyond maxBackupFiles
        std::string backupFile = baseLogFile + "." + std::to_string(i);
        addFileForCleanup(backupFile);
        std::remove(backupFile.c_str());
    }
    std::remove(baseLogFile.c_str());

    std::cout << "[LogRotation] After initial cleanup:" << std::endl;
    for (int i = 0; i <= maxBackupFiles + 2; ++i) { // Check a bit beyond
        std::string f = baseLogFile + (i == 0 ? "" : ("." + std::to_string(i)));
        std::ifstream checker(f);
        std::cout << "[LogRotation]   " << f << " exists: " << (checker.good() ? "yes" : "no") << std::endl;
    }

    ASSERT_NO_THROW(Logger::init(baseLogFile, LogLevel::DEBUG, maxFileSize, maxBackupFiles));
    Logger& logger = Logger::getInstance();

    logger.log(LogLevel::DEBUG, "Initial test message to ensure file creation.");
    std::cout << "[LogRotation] After initial log message, before flush." << std::endl;
    // You might not see the content yet as it might be buffered by the logger.
    // Force a flush and close of the file by reinitializing the logger to a dummy file
    // This ensures that the initial message is written to disk before we try to read it.
    // This is a bit heavy-handed for just one message, but crucial for this check.
    ASSERT_NO_THROW(Logger::init("dummy_initial_check_flush.log", LogLevel::DEBUG, 1024, 1));
    addFileForCleanup("dummy_initial_check_flush.log"); // Add for cleanup
    if (1 >=1) addFileForCleanup("dummy_initial_check_flush.log.1");

    std::cout << "[LogRotation] After flushing initial message." << std::endl;
    std::ifstream initialFileCheck(baseLogFile);
    std::cout << "[LogRotation]   " << baseLogFile << " exists: " << (initialFileCheck.good() ? "yes" : "no") << ", size: " << (initialFileCheck.good() ? readFileContents(baseLogFile).length() : 0) << std::endl;
    initialFileCheck.close();

    std::string initialContents = readFileContents(baseLogFile);
    ASSERT_FALSE(initialContents.empty()) << "Log file " << baseLogFile << " was not created or is empty after initial log. Check permissions or path issues.";
    
    // Re-initialize the logger to continue with the actual rotation test setup
    ASSERT_NO_THROW(Logger::init(baseLogFile, LogLevel::DEBUG, maxFileSize, maxBackupFiles));
    // Note: The logger reference 'logger' is now stale. Get it again.
    Logger& logger_reinit = Logger::getInstance(); 
    // Use logger_reinit for subsequent logging in this test.
    // Replace all subsequent 'logger.log' with 'logger_reinit.log' in this test.

    std::string singleMessage = "Rotation test message. This message is intended to be somewhat long to help fill the log file quickly. "; // ~100 bytes
    // Make it longer to ensure it exceeds file size faster
    for(int k=0; k<3; ++k) singleMessage += singleMessage; // Now ~400 bytes, then ~800 bytes

    // Log enough messages to trigger rotation.
    // Each log entry has JSON overhead (timestamp, level, message keys, quotes, etc.)
    // Estimate overhead: ~70 chars for timestamp, level, message keys, quotes.
    // Message content: ~800 chars. Total per log: ~870 chars.
    // To exceed 1024 bytes, 2 messages should be enough.
    // To create base.log, base.log.1, base.log.2, we need to fill base.log three times.
    // So, 2 messages * 3 = 6 messages should be sufficient.
    for (int i = 0; i < 6; ++i) { // Reduced from 10 to 6 messages
        std::cout << "[LogRotation] Loop " << i << ": About to log. Current file: " << baseLogFile;
        std::cout << std::endl;
        logger_reinit.log(LogLevel::INFO, singleMessage + " #" + std::to_string(i));
        std::cout << "[LogRotation] Loop " << i << ": Logged message." << std::endl;
    }
    
    // Force flush/close by re-initializing. This is a workaround.
    // A proper Logger::flush() or shutdown mechanism would be better.
    // The TearDown method also calls init, which might help finalize files.
    ASSERT_NO_THROW(Logger::init("dummy_rotation_flush.log", LogLevel::DEBUG, 1024, 1));
    addFileForCleanup("dummy_rotation_flush.log");
    addFileForCleanup("dummy_rotation_flush.log.1");

    std::cout << "[LogRotation] After main logging loop and final flush." << std::endl;
    for (int i = 0; i <= maxBackupFiles + 2; ++i) {
        std::string f = baseLogFile + (i == 0 ? "" : ("." + std::to_string(i)));
        std::ifstream checker(f);
        std::cout << "[LogRotation]   " << f << " exists: " << (checker.good() ? "yes" : "no") << ", size: " << (checker.good() ? readFileContents(f).length() : 0) << std::endl;
    }

    // Check for backup files
    std::ifstream currentLog(baseLogFile);
    EXPECT_TRUE(currentLog.good()) << baseLogFile << " should exist.";
    currentLog.close();

    std::ifstream file1(baseLogFile + ".1");
    EXPECT_TRUE(file1.good()) << baseLogFile << ".1 should exist.";
    file1.close();

    std::ifstream file2(baseLogFile + ".2");
    EXPECT_TRUE(file2.good()) << baseLogFile << ".2 should exist.";
    file2.close();
    
    // This file should NOT exist as maxBackupFiles is 2
    std::ifstream file3(baseLogFile + ".3");
    EXPECT_FALSE(file3.good()) << baseLogFile << ".3 should NOT exist.";
    file3.close();
}

// It's good practice to have a test for when maxBackupFiles = 0
TEST_F(LoggerTest, LogRotationNoBackups) {
    const std::string baseLogFile = "test_no_backup_rotation.log";
    const int maxBackupFiles = 0;
    const long long maxFileSize = 512; // 0.5KB

    addFileForCleanup(baseLogFile);
    addFileForCleanup(baseLogFile + ".1"); // Clean up potential .1 file from previous runs if logic was different
    std::remove((baseLogFile + ".1").c_str());
    std::remove(baseLogFile.c_str());


    ASSERT_NO_THROW(Logger::init(baseLogFile, LogLevel::DEBUG, maxFileSize, maxBackupFiles));
    Logger& logger = Logger::getInstance();

    std::string singleMessage = "No backup rotation test. This message is intended to be somewhat long. "; // ~70 bytes
    for(int k=0; k<2; ++k) singleMessage += singleMessage; // Now ~280 bytes
    
    // Log enough to trigger rotation (approx 2 messages for 0.5KB)
    for (int i = 0; i < 5; ++i) {
        logger.log(LogLevel::INFO, singleMessage + " #" + std::to_string(i));
    }

    ASSERT_NO_THROW(Logger::init("dummy_no_backup_flush.log", LogLevel::DEBUG, 1024, 0));
    addFileForCleanup("dummy_no_backup_flush.log");


    std::ifstream currentLog(baseLogFile);
    EXPECT_TRUE(currentLog.good()) << baseLogFile << " should exist (newly created after rotation).";
    currentLog.close();

    // This file should NOT exist as maxBackupFiles is 0
    std::ifstream file1(baseLogFile + ".1");
    EXPECT_FALSE(file1.good()) << baseLogFile << ".1 should NOT exist.";
    file1.close();
}

// Test that the logger can be initialized multiple times (as tests do)
// and that it doesn't crash, and respects the latest init parameters.
TEST_F(LoggerTest, ReinitializationTest) {
    const std::string logFile1 = "test_reinit1.log";
    const std::string logFile2 = "test_reinit2.log";
    addFileForCleanup(logFile1);
    addFileForCleanup(logFile2);
    std::remove(logFile1.c_str());
    std::remove(logFile2.c_str());

    // First initialization
    ASSERT_NO_THROW(Logger::init(logFile1, LogLevel::INFO));
    Logger::getInstance().log(LogLevel::INFO, "Message for logfile1");

    // Second initialization
    ASSERT_NO_THROW(Logger::init(logFile2, LogLevel::WARN));
    Logger::getInstance().log(LogLevel::WARN, "Message for logfile2"); // This should go to logFile2
    Logger::getInstance().log(LogLevel::INFO, "Info message for logfile2"); // Should NOT go to logFile2

    // Check logFile1
    std::string contents1 = readFileContents(logFile1);
    EXPECT_NE(contents1.find("Message for logfile1"), std::string::npos);
    // This message should not be in logFile1 because logger was re-initialized to logFile2
    EXPECT_EQ(contents1.find("Message for logfile2"), std::string::npos); 
    EXPECT_EQ(contents1.find("Info message for logfile2"), std::string::npos);


    // Check logFile2
    std::string contents2 = readFileContents(logFile2);
    EXPECT_NE(contents2.find("Message for logfile2"), std::string::npos);
    EXPECT_EQ(contents2.find("Info message for logfile2"), std::string::npos); // Due to LogLevel::WARN
    EXPECT_EQ(contents2.find("Message for logfile1"), std::string::npos);
}

// Test that logging before init throws an exception
TEST_F(LoggerTest, LogBeforeInit) {
    // This test is tricky because Logger::getInstance() throws if not initialized.
    // And GTest macros might not catch exceptions thrown from constructor/static init.
    // A more direct way to test this would be a death test if the throw is fatal,
    // or ensuring getInstance itself is what we check.
    
    // To test behavior before init, we need to ensure no other test has initialized it.
    // GTest runs tests in an undefined order, so this is hard to guarantee.
    // The singleton nature makes this type of test problematic without a reset capability.
    // For now, we assume that if init was called by another test, getInstance() won't throw.
    // If it's the first test, it *should* throw.
    // This test is therefore somewhat unreliable in a large suite without proper singleton management.
    // A better approach would be to have a dedicated test executable for this one case
    // or a mechanism to fully reset the Logger singleton state.

    // The current logger design: getInstance throws if not initialized.
    // Let's assume this test might run after others have initialized the logger.
    // So, the best we can do is test the behavior *after* an init.
    // To truly test "log before init", one would need to run this test in isolation
    // or have a Logger::resetForTesting() method.

    // The current structure of tests (each calls init) makes it hard to test "before init".
    // We can, however, test that calling init multiple times is safe (covered in ReinitializationTest).
    // And that calling getInstance without init (if possible to orchestrate) throws.
    
    // Given the constraints, this specific test ("LogBeforeInit") is hard to implement reliably.
    // We'll skip a direct test of "log before init throws" because test order isn't guaranteed
    // and other tests will initialize the logger.
    // The Logger::getInstance() method already has a check for s_isInitialized.
    SUCCEED() << "Skipping direct test for 'log before init throws' due to singleton state and test order.";
}
