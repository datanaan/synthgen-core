#include <gtest/gtest.h>
#include "engine/constraint/aggregate_engine.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <limits>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;

namespace {

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
    ColumnDef press;
    press.name = "pressure";
    press.type = DataType::kFloat;
    press.range_min = 900.0;
    press.range_max = 1100.0;
    s.columns.push_back(press);
    return s;
}

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

constexpr int64_t kOneHourUs = 3600000000LL;

AggregateConstraintDef make_avg_constraint(const std::string& col, double max_val,
                                            int64_t window_us = kOneHourUs) {
    AggregateConstraintDef c;
    c.constraint_name = "avg_" + col;
    c.column_name = col;
    c.function = AggregateFunction::kAvg;
    c.max_val = max_val;
    c.window_interval_us = window_us;
    return c;
}

AggregateConstraintDef make_sum_constraint(const std::string& col, double max_val,
                                            int64_t window_us = kOneHourUs) {
    AggregateConstraintDef c;
    c.constraint_name = "sum_" + col;
    c.column_name = col;
    c.function = AggregateFunction::kSum;
    c.max_val = max_val;
    c.window_interval_us = window_us;
    return c;
}

AggregateConstraintDef make_min_constraint(const std::string& col, double min_val,
                                            int64_t window_us = kOneHourUs) {
    AggregateConstraintDef c;
    c.constraint_name = "min_" + col;
    c.column_name = col;
    c.function = AggregateFunction::kMin;
    c.min_val = min_val;
    c.window_interval_us = window_us;
    return c;
}

AggregateConstraintDef make_max_constraint(const std::string& col, double max_val,
                                            int64_t window_us = kOneHourUs) {
    AggregateConstraintDef c;
    c.constraint_name = "max_" + col;
    c.column_name = col;
    c.function = AggregateFunction::kMax;
    c.max_val = max_val;
    c.window_interval_us = window_us;
    return c;
}

AggregateConstraintDef make_count_constraint(const std::string& col, double min_val,
                                              int64_t window_us = kOneHourUs) {
    AggregateConstraintDef c;
    c.constraint_name = "count_" + col;
    c.column_name = col;
    c.function = AggregateFunction::kCount;
    c.min_val = min_val;
    c.window_interval_us = window_us;
    return c;
}

}  // namespace

// ===== Window Computation Edge Cases =====

TEST(AggregateExtended, WindowVerySmallInterval) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});
    // 1 microsecond interval — each row likely in its own window
    auto batch = make_time_table({0, 100, 200, 300}, {10.0, 20.0, 30.0, 40.0});
    auto windows = engine.compute_windows(batch, 1);
    ASSERT_TRUE(windows.ok());
    EXPECT_GE(windows.value().size(), 1u);
}

TEST(AggregateExtended, WindowVeryLargeInterval) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});
    // Huge interval — all rows in one window
    auto batch = make_time_table({0, 100, 200, 300}, {10.0, 20.0, 30.0, 40.0});
    auto windows = engine.compute_windows(batch, INT64_MAX);
    ASSERT_TRUE(windows.ok());
    EXPECT_EQ(windows.value().size(), 1u);
    EXPECT_EQ(windows.value()[0].included_rows.size(), 4u);
}

TEST(AggregateExtended, WindowDuplicateTimestamps) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});
    // Multiple rows with same timestamp
    auto batch = make_time_table({100, 100, 100}, {10.0, 20.0, 30.0});
    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    EXPECT_EQ(windows.value().size(), 1u);
    EXPECT_EQ(windows.value()[0].included_rows.size(), 3u);
}

TEST(AggregateExtended, WindowSingleRowMultipleWindows) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});
    // Each row separated by > 1 hour
    auto batch = make_time_table(
        {0, 2*kOneHourUs, 4*kOneHourUs},
        {10.0, 20.0, 30.0});
    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    EXPECT_EQ(windows.value().size(), 3u);
    for (const auto& w : windows.value()) {
        EXPECT_EQ(w.included_rows.size(), 1u);
    }
}

// ===== Aggregation function edge cases =====

TEST(AggregateExtended, SumAggregateCheck) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_sum_constraint("temperature", 200.0)});
    // Sum = 10+20+30 = 60 < 200 → no violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, SumAggregateViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_sum_constraint("temperature", 50.0)});
    // Sum = 10+20+30 = 60 > 50 → violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, MinAggregateViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_min_constraint("temperature", 15.0)});
    // Min = 10 < 15 → violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, MinAggregatePass) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_min_constraint("temperature", 5.0)});
    // Min = 10 > 5 → no violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, MaxAggregateViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_max_constraint("temperature", 25.0)});
    // Max = 30 > 25 → violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, CountAggregateViolation) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_count_constraint("temperature", 5.0)});
    // Count = 3 < 5 → violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, CountAggregatePass) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_count_constraint("temperature", 3.0)});
    // Count = 3 >= 3 → no violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

// ===== Compute aggregate direct tests =====

TEST(AggregateExtended, ComputeAggregateSum) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kSum;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 60.0);
}

TEST(AggregateExtended, ComputeAggregateMin) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {30.0, 10.0, 20.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kMin;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 10.0);
}

TEST(AggregateExtended, ComputeAggregateSingleRow) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0}, {42.0});
    AggregationWindow window;
    window.included_rows = {0};

    // Avg of single value = that value
    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 42.0);
}

TEST(AggregateExtended, ComputeAggregateEmptyWindow) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000}, {10.0, 20.0});
    AggregationWindow window;
    window.included_rows = {};  // Empty

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;

    auto result = engine.compute_aggregate(batch, window, c);
    // Empty window returns an error
    EXPECT_FALSE(result.ok());
}

TEST(AggregateExtended, ComputeAggregatePartialWindow) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    AggregationWindow window;
    window.included_rows = {0, 2};  // Skip row 1

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 20.0);  // (10 + 30) / 2
}

// ===== Both min and max bounds =====

TEST(AggregateExtended, BothMinAndMaxBounds) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "range_avg";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.min_val = 10.0;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // Avg = 20 → within [10, 50]
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
}

TEST(AggregateExtended, BothBoundsBelowMin) {
    Schema s = make_ordered_schema_with_temp();
    AggregateConstraintDef c;
    c.constraint_name = "range_avg";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.min_val = 50.0;
    c.max_val = 100.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // Avg = 20 < 50 → violation
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

// ===== No order column =====

TEST(AggregateExtended, NoOrderColumnBehavior) {
    Schema s;
    s.type_name = "no_order";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    s.columns.push_back(temp);

    AggregateEngine engine(s, {make_avg_constraint("temperature", 50.0)});
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    // May or may not error depending on implementation — just verify no crash
    // If it returns ok, verify basic structure
    if (result.ok()) {
        EXPECT_GE(result.value().phase_two.total_windows, 0);
    }
}

// ===== Multiple windows spanning hours =====

TEST(AggregateExtended, MultipleWindowsSpanningHours) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_avg_constraint("temperature", 50.0)});

    // Data across 5 hours, 2 rows per hour
    std::vector<int64_t> timestamps;
    std::vector<double> temps;
    for (int h = 0; h < 5; ++h) {
        timestamps.push_back(h * kOneHourUs + 100);
        timestamps.push_back(h * kOneHourUs + 200);
        temps.push_back(20.0 + h * 5);
        temps.push_back(25.0 + h * 5);
    }

    auto batch = make_time_table(timestamps, temps);
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows.size(), 1u);
}

// ===== Timestamp gap larger than interval =====

TEST(AggregateExtended, LargeTimestampGap) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    // Two rows separated by 100 hours
    auto batch = make_time_table({0, 100 * kOneHourUs}, {10.0, 20.0});
    auto windows = engine.compute_windows(batch, kOneHourUs);
    ASSERT_TRUE(windows.ok());
    EXPECT_EQ(windows.value().size(), 2u);
}

// ===== All identical values =====

TEST(AggregateExtended, AllIdenticalValues) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000}, {25.0, 25.0, 25.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 25.0);
}

// ===== Compute aggregate with Sum of identical values =====

TEST(AggregateExtended, SumOfIdenticalValues) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000, 3000, 4000},
                                 {10.0, 10.0, 10.0, 10.0, 10.0});
    AggregationWindow window;
    window.included_rows = {0, 1, 2, 3, 4};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kSum;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 50.0);
}

// ===== Execute with multiple constraints, some violated =====

TEST(AggregateExtended, MultipleConstraintsSomeViolated) {
    Schema s = make_ordered_schema_with_temp();

    AggregateConstraintDef c1;
    c1.constraint_name = "avg_ok";
    c1.column_name = "temperature";
    c1.function = AggregateFunction::kAvg;
    c1.max_val = 100.0;  // Avg=20 < 100 → ok
    c1.window_interval_us = kOneHourUs;

    AggregateConstraintDef c2;
    c2.constraint_name = "max_fail";
    c2.column_name = "temperature";
    c2.function = AggregateFunction::kMax;
    c2.max_val = 15.0;  // Max=30 > 15 → violation
    c2.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c1, c2});
    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().phase_two.windows_violated, 0);
}

// ===== Phase one output =====

TEST(AggregateExtended, PhaseOneOutputTable) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_avg_constraint("temperature", 50.0)});

    auto batch = make_time_table({0, 100, 200}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    // Phase one should produce some output table
    EXPECT_NE(result.value().phase_one_output, nullptr);
}

// ===== Total exclusion rate =====

TEST(AggregateExtended, TotalExclusionRateComputed) {
    Schema s = make_ordered_schema_with_temp();
    AggregateEngine engine(s, {make_avg_constraint("temperature", 5.0)});  // Very restrictive

    auto batch = make_time_table({0, 100, 200}, {40.0, 50.0, 60.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GE(result.value().total_exclusion_rate, 0.0);
}
