#pragma once
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <cstdint>

namespace synthgen::scaffold {

struct TraceSpan {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string component;
    std::string operation;
    int64_t start_time_us = 0;
    int64_t end_time_us = 0;
    std::string status = "ok";
    std::map<std::string, std::string> attributes;
};

class SpanGuard {
public:
    SpanGuard(const std::string& component, const std::string& operation,
              const std::string& trace_id, const std::string& parent_span_id = "");
    ~SpanGuard();

    void set_attribute(const std::string& key, const std::string& value);
    void set_status(const std::string& status);

    const TraceSpan& span() const { return span_; }

    static std::vector<TraceSpan>& active_spans();

private:
    TraceSpan span_;
};

}  // namespace synthgen::scaffold
