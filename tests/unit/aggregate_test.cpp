#include <gtest/gtest.h>
#include "engine/constraint/aggregate_engine.h"
#include "schema/schema.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;

Schema make_ordered_schema_with_temp() {
    Schema s;
    s.type_name = "sensor_log";
    ColumnDef ts;
    ts.name = "timestamp";
    ts.type = DataType::kDatetime;
    ts.is_order = true;
    ts.not_null = true;
    s.columns.push_back(ts);
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    return s;
}

// Helper: timestamps in microseconds, temperature values
std::shared_ptr<arrow::Table> make_time_table(
    const std::vector<int64_t>& timestamps,
    const std::vector<double>& temps) {

    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder temp_builder;
    for (auto t : timestamps) ts_builder.Append(t);
    for (auto v : temps) temp_builder.Append(v);

    auto ts_arr = *ts_builder.Finish();
    auto temp_arr = *temp_builder.Finish();

    auto schema = arrow::schema({
        arrow::field("timestamp", arrow::int64()),
        arrow::field("temperature", arrow::float64())
    });
    return arrow::Table::Make(schema, {ts_arr, temp_arr});
}

// 1 hour = 3600 * 1000000 microseconds
constexpr int64_t kOneHourUs = 3600000000LL;

// ===== Window Computation =====

TEST(AggregateEngineTest, ComputeWindowsOneHour) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    // 3 hours of data, 1 row per hour
    auto batch = make_time_table(
        {0, kOneHourUs, 2*kOneHourUs, 3*kOneHourUs},
        {10.0, 20.0, 30.0, 40.0});

    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    // Each row in its own window (partial except first)
    EXPECT_GE(windows.value().size(), 2u);
}

TEST(AggregateEngineTest, ComputeWindowsRowsInSameWindow) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    // All rows within 1 hour
    auto batch = make_time_table(
        {0, 1000, 2000, 3000},
        {10.0, 20.0, 30.0, 40.0});

    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    // All in one window (partial)
    EXPECT_EQ(windows.value().size(), 1u);
    EXPECT_EQ(windows.value()[0].included_rows.size(), 4u);
    EXPECT_TRUE(windows.value()[0].is_partial);
}

TEST(AggregateEngineTest, ComputeWindowsEmptyBatch) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto empty = arrow::Table::MakeEmpty(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("temperature", arrow::float64())
        })).ValueOrDie();

    auto windows = engine.compute_windows(empty, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    EXPECT_EQ(windows.value().size(), 0u);
}

// ===== Aggregation Functions =====

TEST(AggregateEngineTest, AvgAggregate) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 40.0;

    AggregateEngine engine(s, {c});

    auto batch = make_time_table({0, 1000, 2000}, {30.0, 35.0, 40.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
}

TEST(AggregateEngineTest, SumAggregate) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "sum_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kSum;
    c.max_val = 200.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 1000, 2000}, {50.0, 60.0, 70.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
}

TEST(AggregateEngineTest, MinAggregate) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "min_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kMin;
    c.min_val = -10.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 1000}, {5.0, 10.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
}

TEST(AggregateEngineTest, MaxAggregate) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "max_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kMax;
    c.max_val = 45.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 1000}, {30.0, 40.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
}

TEST(AggregateEngineTest, CountAggregate) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "count_rows";
    c.column_name = "temperature";
    c.function = AggregateFunction::kCount;
    c.min_val = 5.0;  // At least 5 rows per window
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // Only 3 rows in window — count=3 < 5
    auto batch = make_time_table({0, 100, 200}, {1.0, 2.0, 3.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
}

// ===== Constraint Violation Detection =====

TEST(AggregateEngineTest, DetectsViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_limit";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 30.0;  // Max avg = 30
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // Avg = (40+50+60)/3 = 50 > 30 → violation
    auto batch = make_time_table({0, 100, 200}, {40.0, 50.0, 60.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateEngineTest, NoViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_limit";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 100.0;  // Max avg = 100
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

// ===== Phase Two Edge Cases =====

TEST(AggregateEngineTest, PhaseTwoEmptyInput) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto empty = arrow::Table::MakeEmpty(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("temperature", arrow::float64())
        })).ValueOrDie();

    auto result = engine.execute_phase_two(empty);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().total_windows, 0);
}

TEST(AggregateEngineTest, PhaseTwoNullBatch) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto result = engine.execute_phase_two(nullptr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().total_windows, 0);
}

// ===== Multiple Constraints =====

TEST(AggregateEngineTest, MultipleAggregateConstraints) {
    Schema s = make_ordered_schema_with_temp();

    AggregateConstraintDef c1;
    c1.constraint_name = "avg_limit";
    c1.column_name = "temperature";
    c1.function = AggregateFunction::kAvg;
    c1.max_val = 50.0;
    c1.window_interval_us = kOneHourUs;

    AggregateConstraintDef c2;
    c2.constraint_name = "max_limit";
    c2.column_name = "temperature";
    c2.function = AggregateFunction::kMax;
    c2.max_val = 60.0;
    c2.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c1, c2});
    auto batch = make_time_table({0, 100, 200}, {30.0, 40.0, 55.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    // Max=55 < 60 → ok, Avg=(30+40+55)/3=41.7 < 50 → ok
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

// ===== Scaffold =====

TEST(AggregateEngineTest, ProducesTraceSpan) {
    scaffold::SpanGuard::active_spans().clear();

    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 100}, {20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    bool found = false;
    for (const auto& sp : scaffold::SpanGuard::active_spans()) {
        if (sp.component == "aggregate_engine") found = true;
    }
    EXPECT_TRUE(found);
}

TEST(AggregateEngineTest, ExplainReturns) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto info = engine.explain();
    // Just verify it doesn't crash
}

// ===== Window Exclusion Rate =====

TEST(AggregateEngineTest, WindowExclusionRateComputed) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_limit";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 10.0;  // Very restrictive
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 100, 200}, {50.0, 60.0, 70.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    // All windows should violate since avg >> 10
    if (!result.value().phase_two.window_exclusion_rates.empty()) {
        EXPECT_GT(result.value().phase_two.window_exclusion_rates[0].exclusion_rate, 0.0);
    }
}

// ===== Partial Window =====

TEST(AggregateEngineTest, PartialWindowMarked) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    // All data within one window → it's partial
    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    ASSERT_GT(windows.value().size(), 0u);
    EXPECT_TRUE(windows.value()[0].is_partial);
}

TEST(AggregateEngineTest, SingleRowWindow) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_limit";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 100.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0}, {50.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    // Single row → avg = 50 < 100 → no violation
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

// ===== Cross-Hour Boundaries =====

TEST(AggregateEngineTest, CrossHourBoundary) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "avg_limit";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // Data spanning 3 hours
    auto batch = make_time_table(
        {0, kOneHourUs - 1, kOneHourUs, 2*kOneHourUs, 3*kOneHourUs},
        {20.0, 30.0, 40.0, 50.0, 60.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows.size(), 1u);
}

// ===== Compute Aggregate Direct =====

TEST(AggregateEngineTest, ComputeAggregateAvg) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 20.0);
}

TEST(AggregateEngineTest, ComputeAggregateMax) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {10.0, 50.0, 30.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kMax;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 50.0);
}

TEST(AggregateEngineTest, ComputeAggregateCount) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kCount;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 3.0);
}
