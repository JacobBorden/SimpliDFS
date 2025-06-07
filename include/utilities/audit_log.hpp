#ifndef AUDIT_LOG_HPP
#define AUDIT_LOG_HPP

#include <string>
#include <vector>
#include <mutex>
#include <ctime>

/**
 * @brief Stores a chain of audit events.
 *
 * Each event is hashed together with the previous event's hash
 * to create an immutable chain.
 */
class AuditLog {
public:
    /**
     * @brief Representation of a single audit event.
     */
    struct Event {
        std::string type;      ///< Event type (CREATE, WRITE, DELETE)
        std::string file;      ///< Target file name
        std::time_t ts;        ///< Event timestamp
        std::string prevHash;  ///< Hash of previous event
        std::string hash;      ///< Hash of this event
    };

    AuditLog(const AuditLog&) = delete;
    AuditLog& operator=(const AuditLog&) = delete;

    /** Obtain the singleton instance. */
    static AuditLog& getInstance();

    /** Reset the log (primarily for tests). */
    void clear();

    /**
     * @brief Record a CREATE event.
     * @param file File path.
     */
    void recordCreate(const std::string& file);

    /** Record a WRITE event. */
    void recordWrite(const std::string& file);

    /** Record a DELETE event. */
    void recordDelete(const std::string& file);

    /** Access recorded events. */
    const std::vector<Event>& events() const;

    /** Verify the integrity of the chain. */
    bool verify() const;

private:
    AuditLog() = default;
    std::string appendEvent(const std::string& type, const std::string& file);

    mutable std::mutex mutex_;
    std::vector<Event> log_;
};

#endif // AUDIT_LOG_HPP
