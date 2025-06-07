#include "utilities/audit_log.hpp"
#include "utilities/blockio.hpp"
#include <sstream>

AuditLog& AuditLog::getInstance() {
    static AuditLog instance;
    return instance;
}

void AuditLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    log_.clear();
}

void AuditLog::recordCreate(const std::string& file) {
    appendEvent("CREATE", file);
}

void AuditLog::recordWrite(const std::string& file) {
    appendEvent("WRITE", file);
}

void AuditLog::recordDelete(const std::string& file) {
    appendEvent("DELETE", file);
}

const std::vector<AuditLog::Event>& AuditLog::events() const {
    return log_;
}

std::string AuditLog::appendEvent(const std::string& type, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::time_t ts = std::time(nullptr);
    std::string prev = log_.empty() ? std::string() : log_.back().hash;

    std::ostringstream oss;
    oss << ts;
    std::string tsStr = oss.str();

    BlockIO b;
    if(!prev.empty())
        b.ingest(reinterpret_cast<const std::byte*>(prev.data()), prev.size());
    b.ingest(reinterpret_cast<const std::byte*>(type.data()), type.size());
    b.ingest(reinterpret_cast<const std::byte*>(file.data()), file.size());
    b.ingest(reinterpret_cast<const std::byte*>(tsStr.data()), tsStr.size());
    auto dr = b.finalize_hashed();

    Event e{type, file, ts, prev, dr.cid};
    log_.push_back(e);
    return dr.cid;
}

bool AuditLog::verify() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string prev;
    for(const auto& e : log_) {
        std::ostringstream oss;
        oss << e.ts;
        std::string tsStr = oss.str();
        BlockIO b;
        if(!prev.empty())
            b.ingest(reinterpret_cast<const std::byte*>(prev.data()), prev.size());
        b.ingest(reinterpret_cast<const std::byte*>(e.type.data()), e.type.size());
        b.ingest(reinterpret_cast<const std::byte*>(e.file.data()), e.file.size());
        b.ingest(reinterpret_cast<const std::byte*>(tsStr.data()), tsStr.size());
        auto dr = b.finalize_hashed();
        if(dr.cid != e.hash) return false;
        prev = e.hash;
    }
    return true;
}
