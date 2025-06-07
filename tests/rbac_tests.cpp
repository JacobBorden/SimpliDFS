#include <gtest/gtest.h>
#include "utilities/rbac.h"
#include <filesystem>

using namespace SimpliDFS;

static std::string getPolicyPath() {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    p = p.parent_path().parent_path() / "rbac_policy.yaml";
    if (fs::exists(p)) return p.string();
    // Fallback when running directly from source directory
    fs::path alt = fs::path(__FILE__).parent_path().parent_path() / "rbac_policy.yaml";
    return alt.string();
}

TEST(RBACPolicyTest, LoadFromFileSuccess) {
    RBACPolicy policy;
    EXPECT_TRUE(policy.loadFromFile(getPolicyPath()));
}

TEST(RBACPolicyTest, LoadFromFileFailure) {
    RBACPolicy policy;
    EXPECT_FALSE(policy.loadFromFile("nonexistent.yaml"));
}

TEST(RBACPolicyTest, PermissionChecks) {
    RBACPolicy policy;
    ASSERT_TRUE(policy.loadFromFile(getPolicyPath()));

    // Admin users
    EXPECT_TRUE(policy.isAllowed(0, "read"));
    EXPECT_TRUE(policy.isAllowed(0, "write"));
    EXPECT_TRUE(policy.isAllowed(0, "delete"));
    EXPECT_TRUE(policy.isAllowed(1000, "write"));

    // Reader user
    EXPECT_TRUE(policy.isAllowed(1001, "read"));
    EXPECT_FALSE(policy.isAllowed(1001, "write"));
    EXPECT_FALSE(policy.isAllowed(1001, "delete"));

    // Unknown user and operation
    EXPECT_FALSE(policy.isAllowed(9999, "read"));
    EXPECT_FALSE(policy.isAllowed(0, "execute"));
}

