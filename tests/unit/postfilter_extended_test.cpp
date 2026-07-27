#include <gtest/gtest.h>
#include "engine/postfilter/post_filter.h"
#include "scaffold/trace.h"

#include <arrow/table.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <limits>

using namespace synthgen::engine::postfilter;

namespace {

std::shared_ptr<arrow::Table> make_table(int64_t rows) {
    arrow::DoubleBuilder builder;
    for (int64_t i = 0; i < rows; ++i) builder.Append(static_cast<double>(i));
    auto arr = *builder.Finish();
    auto schema = arrow::schema({arrow::field("value", arrow::float64())});
    return arrow::Table::Make(schema, {arr});
}

}  // namespace

// ===== Exclusion Rate Boundary Tests =====

TEST(PostFilterExtended, ClassifyExactlyAtLowThreshold_0_30) {
    // rate > 0.30 is medium; rate == 0.30 is still low (strict >)
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.30, 0.9), ExclusionRateBand::kLow);
}

TEST(PostFilterExtended, ClassifyJustBelowLowThreshold) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.299, 0.9), ExclusionRateBand::kLow);
}

TEST(PostFilterExtended, ClassifyExactlyAtMediumHigh_0_70) {
    // rate > 0.70 is high; rate == 0.70 is medium (strict >)
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.70, 0.9), ExclusionRateBand::kMedium);
}

TEST(PostFilterExtended, ClassifyJustBelowMediumHigh) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.699, 0.9), ExclusionRateBand::kMedium);
}

TEST(PostFilterExtended, ClassifyExactlyAtCriticalThreshold) {
    // rate > critical_threshold is critical; rate == critical is high (strict >)
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.90, 0.9), ExclusionRateBand::kHigh);
}

TEST(PostFilterExtended, ClassifyJustBelowCriticalThreshold) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.899, 0.9), ExclusionRateBand::kHigh);
}

// ===== Extreme exclusion rates =====

TEST(PostFilterExtended, ClassifyExactlyZero) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.0, 0.9), ExclusionRateBand::kLow);
}

TEST(PostFilterExtended, ClassifyExactlyOne) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(1.0, 0.9), ExclusionRateBand::kCritical);
}

TEST(PostFilterExtended, ClassifyNegativeRate) {
    // Negative rate — implementation-defined but shouldn't crash
    auto band = PostFilter::classify_exclusion_rate(-0.1, 0.9);
    // Just verify no crash
}

TEST(PostFilterExtended, ClassifyRateAboveOne) {
    auto band = PostFilter::classify_exclusion_rate(1.5, 0.9);
    // Just verify no crash
}

// ===== Custom critical threshold =====

TEST(PostFilterExtended, CustomCriticalThreshold) {
    // Custom threshold at 0.5 → 0.6 > 0.5 → critical
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.6, 0.5), ExclusionRateBand::kCritical);
    // 0.4 > 0.30 → medium; 0.4 not > 0.70 → medium
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.4, 0.5), ExclusionRateBand::kMedium);
}

TEST(PostFilterExtended, VeryLowCriticalThreshold) {
    // Very low threshold → almost everything is critical
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.1, 0.05), ExclusionRateBand::kCritical);
}

TEST(PostFilterExtended, VeryHighCriticalThreshold) {
    // Very high threshold → nothing is critical until very high
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.95, 0.99), ExclusionRateBand::kHigh);
    // 0.99 == 0.99, not >, so high
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.99, 0.99), ExclusionRateBand::kHigh);
}

// ===== Data grade mapping =====

TEST(PostFilterExtended, AllDataGrades) {
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kLow), "statistics_guaranteed");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kMedium), "limited_fidelity");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kHigh), "limited_fidelity_conservative");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kCritical), "rejected");
}

// ===== Execute edge cases =====

TEST(PostFilterExtended, ExecuteNegativeTargetRows) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, -1);
    ASSERT_TRUE(result.ok());
    // Negative target treated as zero
}

TEST(PostFilterExtended, ExecuteTargetExceedsAvailable) {
    PostFilter pf;
    auto table = make_table(10);
    auto result = pf.execute(table, 1000);
    ASSERT_TRUE(result.ok());
    // Should handle gracefully
}

TEST(PostFilterExtended, ExecuteTargetEqualsAvailable) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, 100);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().pre_filter_rows, 100);
}

TEST(PostFilterExtended, ExecuteTargetOne) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, 1);
    // target=1 out of 100 → exclusion_rate = 99/100 = 0.99 → critical → may fail
    if (result.ok()) {
        EXPECT_GT(result.value().pre_filter_rows, 0);
    }
    // If critical, it returns an error — that's also valid behavior
}

TEST(PostFilterExtended, ExecuteEmptyTable) {
    PostFilter pf;
    auto empty = arrow::Table::MakeEmpty(
        arrow::schema({arrow::field("value", arrow::float64())})).ValueOrDie();
    auto result = pf.execute(empty, 10);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().pre_filter_rows, 0);
}

// ===== Config edge cases =====

TEST(PostFilterExtended, ConfigZeroTimeout) {
    PostFilterConfig config;
    config.timeout_ms = 0;
    PostFilter pf(config);
    auto table = make_table(100);
    auto result = pf.execute(table, 50);
    ASSERT_TRUE(result.ok());
}

TEST(PostFilterExtended, ConfigVerySmallCriticalThreshold) {
    PostFilterConfig config;
    config.critical_exclusion_threshold = 0.01;
    PostFilter pf(config);
    EXPECT_DOUBLE_EQ(pf.config().critical_exclusion_threshold, 0.01);
}

TEST(PostFilterExtended, ConfigRealtimeMonitoringDisabled) {
    PostFilterConfig config;
    config.enable_realtime_monitoring = false;
    PostFilter pf(config);
    auto table = make_table(100);
    auto result = pf.execute(table, 50);
    ASSERT_TRUE(result.ok());
    // With monitoring disabled, series might be empty
}

// ===== PostFilterResult structure =====

TEST(PostFilterExtended, ResultDefaultValues) {
    PostFilterResult r;
    EXPECT_EQ(r.pre_filter_rows, 0);
    EXPECT_EQ(r.post_filter_rows, 0);
    EXPECT_DOUBLE_EQ(r.actual_exclusion_rate, 0.0);
    EXPECT_EQ(r.rate_band, ExclusionRateBand::kLow);
    EXPECT_TRUE(r.data_grade.empty());
    EXPECT_FALSE(r.was_timeout_truncated);
    EXPECT_TRUE(r.realtime_exclusion_rate_series.empty());
    EXPECT_EQ(r.processing_time_ms, 0);
}

// ===== Multiple executes on same filter =====

TEST(PostFilterExtended, MultipleExecutesSameFilter) {
    PostFilter pf;
    for (int i = 0; i < 5; ++i) {
        auto table = make_table(100);
        auto result = pf.execute(table, 50);
        ASSERT_TRUE(result.ok()) << "Failed on iteration " << i;
    }
}

// ===== ExclusionGradeMapping structure =====

TEST(PostFilterExtended, ExclusionGradeMappingStructure) {
    ExclusionGradeMapping m;
    m.band = ExclusionRateBand::kLow;
    m.rate_min = 0.0;
    m.rate_max = 0.30;
    m.data_grade = "statistics_guaranteed";
    m.behavior = "allow";
    m.allow_post_filter = true;

    EXPECT_EQ(m.band, ExclusionRateBand::kLow);
    EXPECT_DOUBLE_EQ(m.rate_min, 0.0);
    EXPECT_TRUE(m.allow_post_filter);
}

// ===== Large batch =====

TEST(PostFilterExtended, LargeBatch) {
    PostFilter pf;
    auto table = make_table(10000);
    auto result = pf.execute(table, 5000);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().pre_filter_rows, 10000);
}
