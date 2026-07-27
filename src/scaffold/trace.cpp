#include "scaffold/trace.h"
#include <atomic>
#include <random>
#include <sstream>

namespace synthgen::scaffold {

namespace {
std::string generate_span_id() {
    static std::atomic<uint64_t> counter{0};
    uint64_t val = counter.fetch_add(1);
    std::ostringstream oss;
    oss << std::hex << val;
    return oss.str();
}

int64_t now_us() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}
}  // namespace

std::vector<TraceSpan>& SpanGuard::active_spans() {
    static thread_local std::vector<TraceSpan> spans;
    return spans;
}

SpanGuard::SpanGuard(const std::string& component, const std::string& operation,
                     const std::string& trace_id, const std::string& parent_span_id) {
    span_.trace_id = trace_id;
    span_.span_id = generate_span_id();
    span_.parent_span_id = parent_span_id;
    span_.component = component;
    span_.operation = operation;
    span_.start_time_us = now_us();
    span_.status = "ok";
}

SpanGuard::~SpanGuard() {
    span_.end_time_us = now_us();
    active_spans().push_back(span_);
}

void SpanGuard::set_attribute(const std::string& key, const std::string& value) {
    span_.attributes[key] = value;
}

void SpanGuard::set_status(const std::string& status) {
    span_.status = status;
}

}  // namespace synthgen::scaffold
