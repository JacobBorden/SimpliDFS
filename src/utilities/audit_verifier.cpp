#include "utilities/audit_verifier.hpp"
#include <thread>

AuditVerifier::AuditVerifier(AuditLog& log, std::chrono::seconds interval)
    : log_(log), interval_(interval) {}

AuditVerifier::~AuditVerifier() { stop(); }

void AuditVerifier::start() {
    if(running_) return;
    running_ = true;
    worker_ = std::thread(&AuditVerifier::threadFunc, this);
}

void AuditVerifier::stop() {
    if(!running_) return;
    running_ = false;
    if(worker_.joinable()) worker_.join();
}

bool AuditVerifier::verifyOnce() {
    return log_.verify();
}

void AuditVerifier::threadFunc() {
    while(running_) {
        verifyOnce();
        for(std::chrono::seconds s{0}; s < interval_ && running_; s += std::chrono::seconds(1)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}
