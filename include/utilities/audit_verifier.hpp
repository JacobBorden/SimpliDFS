#ifndef AUDIT_VERIFIER_HPP
#define AUDIT_VERIFIER_HPP

#include "utilities/audit_log.hpp"
#include <thread>
#include <atomic>
#include <chrono>

/**
 * @brief Periodically verifies the audit log chain.
 */
class AuditVerifier {
public:
    AuditVerifier(AuditLog& log, std::chrono::seconds interval);
    ~AuditVerifier();

    /** Start background verification. */
    void start();
    /** Stop background verification. */
    void stop();
    /** Run a single verification pass. */
    bool verifyOnce();

private:
    void threadFunc();

    AuditLog& log_;
    std::chrono::seconds interval_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

#endif // AUDIT_VERIFIER_HPP
