#pragma once

#include "common/result.h"
#include "common/types.h"
#include "scaffold/explain.h"

#include <arrow/table.h>
#include <string>
#include <vector>
#include <cstdint>

namespace synthgen::engine::postfilter {

enum class ExclusionRateBand {
    kLow,
    kMedium,
    kHigh,
    kCritical,
};

struct ExclusionGradeMapping {
    ExclusionRateBand band;
    double rate_min;
    double rate_max;
    std::string data_grade;
    std::string behavior;
    bool allow_post_filter;
};

struct PostFilterConfig {
    double timeout_ms = 30000.0;
    double high_exclusion_threshold = 0.80;
    double critical_exclusion_threshold = 0.90;
    bool enable_realtime_monitoring = true;
    double oversampling_ratio = 3.0;
};

struct PostFilterResult {
    std::shared_ptr<arrow::Table> filtered_data;
    int64_t pre_filter_rows = 0;
    int64_t post_filter_rows = 0;
    double actual_exclusion_rate = 0.0;
    ExclusionRateBand rate_band = ExclusionRateBand::kLow;
    std::string data_grade;
    bool was_timeout_truncated = false;
    std::vector<double> realtime_exclusion_rate_series;
    int64_t processing_time_ms = 0;
};

class PostFilter {
public:
    explicit PostFilter(const PostFilterConfig& config = {});

    Result<PostFilterResult> execute(
        std::shared_ptr<arrow::Table> sampled_data,
        int64_t target_rows);

    static ExclusionRateBand classify_exclusion_rate(double rate, double critical_threshold);
    static std::string data_grade_for_band(ExclusionRateBand band);

    const PostFilterConfig& config() const { return config_; }

private:
    PostFilterConfig config_;
};

}  // namespace synthgen::engine::postfilter
