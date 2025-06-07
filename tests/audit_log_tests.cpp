#include <gtest/gtest.h>
#include "utilities/filesystem.h"
#include "utilities/audit_log.hpp"
#include "utilities/audit_verifier.hpp"

TEST(AuditLog, ChainIntegrity) {
    AuditLog& log = AuditLog::getInstance();
    log.clear();

    FileSystem fs;
    ASSERT_TRUE(fs.createFile("a"));
    ASSERT_TRUE(fs.writeFile("a", "data"));
    ASSERT_TRUE(fs.deleteFile("a"));

    EXPECT_EQ(log.events().size(), 3u);
    EXPECT_TRUE(log.verify());

    AuditVerifier verifier(log, std::chrono::seconds(0));
    EXPECT_TRUE(verifier.verifyOnce());
}
