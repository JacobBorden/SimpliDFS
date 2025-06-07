#pragma once
#include <string>
#include <map>
#include <mutex>
#include <unordered_map>

/**
 * @brief Simple metrics registry that exports Prometheus text format.
 */
class MetricsRegistry {
public:
    /** Get singleton instance. */
    static MetricsRegistry& instance();

    /** Set gauge value with optional labels. */
    void setGauge(const std::string& name, double value,
                  const std::map<std::string, std::string>& labels = {});

    /** Increment counter by value (default 1). */
    void incrementCounter(const std::string& name, double value = 1.0,
                          const std::map<std::string, std::string>& labels = {});

    /** Record observation for a histogram. */
    void observe(const std::string& name, double value,
                 const std::map<std::string, std::string>& labels = {});

    /** Serialize all metrics in Prometheus text format. */
    std::string toPrometheus() const;

    /**
     * @brief Clear all stored metrics.
     *
     * Primarily used by unit tests to ensure a clean registry state.
     */
    void reset();

    /** Convert labels map to Prometheus label string. */
    static std::string labelsToString(const std::map<std::string,std::string>& labels);

private:
    MetricsRegistry() = default;
    struct Histogram { double sum{0}; unsigned long count{0}; };

    mutable std::mutex mtx_;
    std::unordered_map<std::string, double> gauges_;
    std::unordered_map<std::string, double> counters_;
    std::unordered_map<std::string, Histogram> histograms_;

};

