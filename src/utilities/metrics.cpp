#include "utilities/metrics.h"
#include <sstream>
#include <algorithm>

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry inst;
    return inst;
}

static std::string makeKey(const std::string& name, const std::map<std::string,std::string>& labels) {
    std::ostringstream oss; oss << name << MetricsRegistry::labelsToString(labels);
    return oss.str();
}

void MetricsRegistry::setGauge(const std::string& name, double value,
                               const std::map<std::string,std::string>& labels) {
    std::lock_guard<std::mutex> lg(mtx_);
    gauges_[makeKey(name, labels)] = value;
}

void MetricsRegistry::incrementCounter(const std::string& name, double value,
                                       const std::map<std::string,std::string>& labels) {
    std::lock_guard<std::mutex> lg(mtx_);
    counters_[makeKey(name, labels)] += value;
}

void MetricsRegistry::observe(const std::string& name, double value,
                              const std::map<std::string,std::string>& labels) {
    std::lock_guard<std::mutex> lg(mtx_);
    auto& h = histograms_[makeKey(name, labels)];
    h.sum += value; h.count += 1;
}

std::string MetricsRegistry::labelsToString(const std::map<std::string,std::string>& labels) {
    if(labels.empty()) return "";
    std::ostringstream oss;
    oss << '{';
    bool first = true;
    for (const auto& kv : labels) {
        if(!first) oss << ',';
        first = false;
        oss << kv.first << "=\"" << kv.second << "\"";
    }
    oss << '}';
    return oss.str();
}

std::string MetricsRegistry::toPrometheus() const {
    std::lock_guard<std::mutex> lg(mtx_);
    std::ostringstream oss;
    for (const auto& kv : gauges_) {
        auto name_end = kv.first.find('{');
        std::string name = kv.first.substr(0, name_end);
        std::string labels = name_end == std::string::npos ? "" : kv.first.substr(name_end);
        oss << name << labels << ' ' << kv.second << '\n';
    }
    for (const auto& kv : counters_) {
        auto name_end = kv.first.find('{');
        std::string name = kv.first.substr(0, name_end);
        std::string labels = name_end == std::string::npos ? "" : kv.first.substr(name_end);
        oss << name << labels << ' ' << kv.second << '\n';
    }
    for (const auto& kv : histograms_) {
        auto name_end = kv.first.find('{');
        std::string name = kv.first.substr(0, name_end);
        std::string labels = name_end == std::string::npos ? "" : kv.first.substr(name_end);
        oss << name << "_sum" << labels << ' ' << kv.second.sum << '\n';
        oss << name << "_count" << labels << ' ' << kv.second.count << '\n';
    }
    return oss.str();
}

void MetricsRegistry::reset() {
    std::lock_guard<std::mutex> lg(mtx_);
    gauges_.clear();
    counters_.clear();
    histograms_.clear();
}
