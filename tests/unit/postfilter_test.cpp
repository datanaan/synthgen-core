#include <gtest/gtest.h>
#include "engine/postfilter/post_filter.h"
#include "scaffold/trace.h"

#include <arrow/table.h>
#include <arrow/builder.h>
#include <arrow/type.h>

using namespace synthgen::engine::postfilter;

std::shared_ptr<arrow::Table> make_table(int64_t rows) {
    arrow::DoubleBuilder builder;
    for (int64_t i = 0; i < rows; ++i) builder.Append(static_cast<double>(i));
    auto arr = *builder.Finish();
    auto schema = arrow::schema({arrow::field("value", arrow::float64())});
    return arrow::Table::Make(schema, {arr});
}

// ===== Exclusion Rate Classification =====

TEST(PostFilterTest, ClassifyLow) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.1, 0.9), ExclusionRateBand::kLow);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.0, 0.9), ExclusionRateBand::kLow);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.29, 0.9), ExclusionRateBand::kLow);
}

TEST(PostFilterTest, ClassifyMedium) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.31, 0.9), ExclusionRateBand::kMedium);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.5, 0.9), ExclusionRateBand::kMedium);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.69, 0.9), ExclusionRateBand::kMedium);
}

TEST(PostFilterTest, ClassifyHigh) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.71, 0.9), ExclusionRateBand::kHigh);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.85, 0.9), ExclusionRateBand::kHigh);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.89, 0.9), ExclusionRateBand::kHigh);
}

TEST(PostFilterTest, ClassifyCritical) {
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.91, 0.9), ExclusionRateBand::kCritical);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.99, 0.9), ExclusionRateBand::kCritical);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(1.0, 0.9), ExclusionRateBand::kCritical);
}

// ===== Data Grade Mapping =====

TEST(PostFilterTest, DataGradeLow) {
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kLow),
              "statistics_guaranteed");
}

TEST(PostFilterTest, DataGradeMedium) {
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kMedium),
              "limited_fidelity");
}

TEST(PostFilterTest, DataGradeHigh) {
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kHigh),
              "limited_fidelity_conservative");
}

TEST(PostFilterTest, DataGradeCritical) {
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kCritical),
              "rejected");
}

// ===== Execute: Normal =====

TEST(PostFilterTest, ExecutePassThrough) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, 100);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().pre_filter_rows, 100);
    EXPECT_GE(result.value().post_filter_rows, 0);
}

TEST(PostFilterTest, ExecuteEmptyInput) {
    PostFilter pf;
    auto result = pf.execute(nullptr, 100);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().pre_filter_rows, 0);
    EXPECT_EQ(result.value().post_filter_rows, 0);
}

TEST(PostFilterTest, ExecuteZeroTarget) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, 0);
    ASSERT_TRUE(result.ok());
}

// ===== Execute: Processing Time =====

TEST(PostFilterTest, ProcessingTimeRecorded) {
    PostFilter pf;
    auto table = make_table(100);
    auto result = pf.execute(table, 100);
    ASSERT_TRUE(result.ok());
    EXPECT_GE(result.value().processing_time_ms, 0);
}

// ===== Config =====

TEST(PostFilterTest, DefaultConfig) {
    PostFilter pf;
    EXPECT_DOUBLE_EQ(pf.config().timeout_ms, 30000.0);
    EXPECT_DOUBLE_EQ(pf.config().critical_exclusion_threshold, 0.90);
    EXPECT_DOUBLE_EQ(pf.config().oversampling_ratio, 3.0);
}

TEST(PostFilterTest, CustomConfig) {
    PostFilterConfig config;
    config.timeout_ms = 5000;
    config.critical_exclusion_threshold = 0.95;
    PostFilter pf(config);
    EXPECT_DOUBLE_EQ(pf.config().timeout_ms, 5000);
    EXPECT_DOUBLE_EQ(pf.config().critical_exclusion_threshold, 0.95);
}

// ===== Realtime Monitoring =====

TEST(PostFilterTest, RealtimeExclusionRateSeries) {
    PostFilter pf;
    auto table = make_table(1000);
    auto result = pf.execute(table, 100);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().realtime_exclusion_rate_series.empty());
}

// ===== Trace =====

TEST(PostFilterTest, ProducesTraceSpan) {
    synthgen::scaffold::SpanGuard::active_spans().clear();
    PostFilter pf;
    auto table = make_table(10);
    auto result = pf.execute(table, 10);
    ASSERT_TRUE(result.ok());

    bool found = false;
    for (const auto& sp : synthgen::scaffold::SpanGuard::active_spans()) {
        if (sp.component == "postfilter") found = true;
    }
    EXPECT_TRUE(found);
}
