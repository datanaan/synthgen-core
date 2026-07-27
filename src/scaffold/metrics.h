#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace synthgen::scaffold {

class Counter {
public:
    explicit Counter(const std::string& name);
    void increment(int64_t delta = 1);
    int64_t value() const;
    const std::string& name() const;
private:
    std::string name_;
    std::atomic<int64_t> value_{0};
};

class Gauge {
public:
    explicit Gauge(const std::string& name);
    void set(double val);
    double value() const;
    const std::string& name() const;
private:
    std::string name_;
    std::atomic<double> value_{0.0};
};

class Histogram {
public:
    explicit Histogram(const std::string& name);
    void observe(double value);
    double mean() const;
    int64_t count() const;
    double sum() const;
    const std::string& name() const;
private:
    std::string name_;
    std::atomic<int64_t> count_{0};
    std::atomic<double> sum_{0.0};
};

class MetricsRegistry {
public:
    static MetricsRegistry& instance();
    Counter& counter(const std::string& name);
    Gauge& gauge(const std::string& name);
    Histogram& histogram(const std::string& name);
    std::map<std::string, int64_t> all_counters() const;
    std::map<std::string, double> all_gauges() const;
    void reset();
private:
    MetricsRegistry() = default;
    std::map<std::string, std::unique_ptr<Counter>> counters_;
    std::map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::map<std::string, std::unique_ptr<Histogram>> histograms_;
    mutable std::mutex mutex_;
};

}  // namespace synthgen::scaffold
