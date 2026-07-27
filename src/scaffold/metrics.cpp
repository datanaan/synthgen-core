#include "scaffold/metrics.h"

namespace synthgen::scaffold {

// --- Counter ---
Counter::Counter(const std::string& name) : name_(name) {}
void Counter::increment(int64_t delta) { value_.fetch_add(delta, std::memory_order_relaxed); }
int64_t Counter::value() const { return value_.load(std::memory_order_relaxed); }
const std::string& Counter::name() const { return name_; }

// --- Gauge ---
Gauge::Gauge(const std::string& name) : name_(name) {}
void Gauge::set(double val) { value_.store(val, std::memory_order_relaxed); }
double Gauge::value() const { return value_.load(std::memory_order_relaxed); }
const std::string& Gauge::name() const { return name_; }

// --- Histogram ---
Histogram::Histogram(const std::string& name) : name_(name) {}
void Histogram::observe(double value) {
    count_.fetch_add(1, std::memory_order_relaxed);
    double old_sum = sum_.load(std::memory_order_relaxed);
    while (!sum_.compare_exchange_weak(old_sum, old_sum + value,
                                        std::memory_order_relaxed)) {}
}
double Histogram::mean() const {
    int64_t c = count_.load(std::memory_order_relaxed);
    return c > 0 ? sum_.load(std::memory_order_relaxed) / c : 0.0;
}
int64_t Histogram::count() const { return count_.load(std::memory_order_relaxed); }
double Histogram::sum() const { return sum_.load(std::memory_order_relaxed); }
const std::string& Histogram::name() const { return name_; }

// --- MetricsRegistry ---
MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry reg;
    return reg;
}

Counter& MetricsRegistry::counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ptr = counters_[name];
    if (!ptr) ptr = std::make_unique<Counter>(name);
    return *ptr;
}

Gauge& MetricsRegistry::gauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ptr = gauges_[name];
    if (!ptr) ptr = std::make_unique<Gauge>(name);
    return *ptr;
}

Histogram& MetricsRegistry::histogram(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& ptr = histograms_[name];
    if (!ptr) ptr = std::make_unique<Histogram>(name);
    return *ptr;
}

std::map<std::string, int64_t> MetricsRegistry::all_counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, int64_t> result;
    for (auto& [name, ctr] : counters_) {
        result[name] = ctr->value();
    }
    return result;
}

std::map<std::string, double> MetricsRegistry::all_gauges() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, double> result;
    for (auto& [name, g] : gauges_) {
        result[name] = g->value();
    }
    return result;
}

void MetricsRegistry::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear();
    gauges_.clear();
    histograms_.clear();
}

}  // namespace synthgen::scaffold
