#include <gtest/gtest.h>
#include "utilities/metrics.h"

/**
 * @brief Verify correct formatting of label strings in Prometheus format.
 */
TEST(MetricsRegistry, LabelsToString) {
    std::map<std::string,std::string> labels{{"k","v"},{"a","b"}};
    std::string formatted = MetricsRegistry::labelsToString(labels);
    // Map iteration is ordered, so "a" should come before "k".
    EXPECT_EQ(formatted, "{a=\"b\",k=\"v\"}");
}

/**
 * @brief Validate gauge, counter and histogram reporting.
 */
TEST(MetricsRegistry, BasicRecording) {
    MetricsRegistry::instance().reset();
    // Record a gauge with a label
    MetricsRegistry::instance().setGauge("gauge", 2.5, {{"host","localhost"}});
    // Increment a counter
    MetricsRegistry::instance().incrementCounter("requests_total", 3);
    // Record a histogram observation
    MetricsRegistry::instance().observe("latency_seconds", 1.2);
    std::string metrics = MetricsRegistry::instance().toPrometheus();
    EXPECT_NE(metrics.find("gauge{host=\"localhost\"} 2.5"), std::string::npos);
    EXPECT_NE(metrics.find("requests_total 3"), std::string::npos);
    EXPECT_NE(metrics.find("latency_seconds_sum 1.2"), std::string::npos);
    EXPECT_NE(metrics.find("latency_seconds_count 1"), std::string::npos);
    MetricsRegistry::instance().reset();
    EXPECT_TRUE(MetricsRegistry::instance().toPrometheus().empty());
}
