#include "engine/postfilter/post_filter.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <chrono>
#include <algorithm>

namespace synthgen::engine::postfilter {

PostFilter::PostFilter(const PostFilterConfig& config)
    : config_(config) {}

ExclusionRateBand PostFilter::classify_exclusion_rate(
    double rate, double critical_threshold) {
    if (rate > critical_threshold) return ExclusionRateBand::kCritical;
    if (rate > 0.70) return ExclusionRateBand::kHigh;
    if (rate > 0.30) return ExclusionRateBand::kMedium;
    return ExclusionRateBand::kLow;
}

std::string PostFilter::data_grade_for_band(ExclusionRateBand band) {
    switch (band) {
        case ExclusionRateBand::kLow:
            return "statistics_guaranteed";
        case ExclusionRateBand::kMedium:
            return "limited_fidelity";
        case ExclusionRateBand::kHigh:
            return "limited_fidelity_conservative";
        case ExclusionRateBand::kCritical:
            return "rejected";
    }
    return "unknown";
}

Result<PostFilterResult> PostFilter::execute(
    std::shared_ptr<arrow::Table> sampled_data,
    int64_t target_rows) {

    scaffold::SpanGuard span("postfilter", "execute", "pf_exec");

    auto start = std::chrono::steady_clock::now();

    PostFilterResult result;
    result.pre_filter_rows = sampled_data ? sampled_data->num_rows() : 0;

    if (!sampled_data || sampled_data->num_rows() == 0 || target_rows <= 0) {
        result.post_filter_rows = 0;
        result.actual_exclusion_rate = 0.0;
        result.rate_band = ExclusionRateBand::kLow;
        result.data_grade = "statistics_guaranteed";
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        result.processing_time_ms = elapsed;
        return result;
    }

    // Check if exclusion rate would be too high
    // If we have far fewer rows than target, rate would be high
    int64_t available_rows = sampled_data->num_rows();
    double estimated_rate = 1.0 - (static_cast<double>(std::min(available_rows, target_rows)) /
                                    static_cast<double>(available_rows));

    auto band = classify_exclusion_rate(estimated_rate, config_.critical_exclusion_threshold);

    if (band == ExclusionRateBand::kCritical) {
        return Error(ErrorCode::kInvalidState,
                     "Exclusion rate would exceed critical threshold (" +
                     std::to_string(static_cast<int>(estimated_rate * 100)) +
                     "%), post-filter rejected",
                     "postfilter");
    }

    // For now, pass through the data (actual constraint filtering is done by
    // ValueRangeValidator / InterRowEngine / AggregateEngine separately)
    // The PostFilter's job is to track rates and apply thresholds

    result.filtered_data = sampled_data;
    result.post_filter_rows = std::min(available_rows, target_rows);
    result.actual_exclusion_rate = estimated_rate;
    result.rate_band = band;
    result.data_grade = data_grade_for_band(band);

    // Real-time monitoring: record exclusion rate at intervals
    // Note: In current pass-through mode, the rate is constant throughout.
    // When actual constraint filtering is implemented, this will record
    // the evolving exclusion rate as rows are processed.
    if (config_.enable_realtime_monitoring && available_rows > 0) {
        int steps = std::min(static_cast<int>(available_rows), 10);
        for (int i = 1; i <= steps; ++i) {
            // Pass-through mode: rate is constant, no actual filtering occurs
            result.realtime_exclusion_rate_series.push_back(estimated_rate);
        }
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    result.processing_time_ms = elapsed;

    scaffold::MetricsRegistry::instance().counter("postfilter_total").increment();
    span.set_attribute("exclusion_rate", std::to_string(result.actual_exclusion_rate));

    return result;
}

}  // namespace synthgen::engine::postfilter
