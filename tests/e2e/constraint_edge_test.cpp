// E2E Constraint Edge Tests — Chaos round 2: InterRow + Aggregate + Classifier + Router
// Targets edge cases that unit tests missed.
#include <gtest/gtest.h>

#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "schema/schema.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <cmath>
#include <vector>
#include <string>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::router;
using namespace synthgen::schema;

// ===== Helpers =====

Schema make_ordered_schema() {
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
    ColumnDef vib;
    vib.name = "vibration";
    vib.type = DataType::kFloat;
    s.columns.push_back(vib);
    return s;
}

Schema make_agg_schema() {
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
    s.columns.push_back(temp);
    return s;
}

std::shared_ptr<arrow::Table> make_table(
    const std::vector<double>& temp_values,
    const std::vector<double>& vib_values = {}) {

    int64_t rows = static_cast<int64_t>(temp_values.size());
    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder temp_builder;
    arrow::DoubleBuilder vib_builder;

    for (int64_t i = 0; i < rows; ++i) ts_builder.Append(i);
    for (auto v : temp_values) temp_builder.Append(v);
    if (vib_values.empty()) {
        for (int64_t i = 0; i < rows; ++i) vib_builder.Append(0.0);
    } else {
        for (auto v : vib_values) vib_builder.Append(v);
    }

    auto ts_arr = *ts_builder.Finish();
    auto temp_arr = *temp_builder.Finish();
    auto vib_arr = *vib_builder.Finish();

    auto schema = arrow::schema({
        arrow::field("timestamp", arrow::int64()),
        arrow::field("temperature", arrow::float64()),
        arrow::field("vibration", arrow::float64())
    });

    return arrow::Table::Make(schema, {ts_arr, temp_arr, vib_arr});
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

// ===== Test 1: DeltaMin filters values that are too close together =====

TEST(ConstraintEdgeTest, DeltaMinFiltersCloseValues) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMin;
    c.delta_min = 5.0;

    InterRowEngine engine(s, {c});
    // diffs: |2-1|=1, |3-2|=1, |10-3|=7, |11-10|=1
    // Row 0: no prev -> passes
    // Row 1: |2-1|=1 < 5 -> filtered
    // Row 2: |3-2|=1 < 5 -> filtered
    // Row 3: prev passing = row 0 (value 1.0), |10-1|=9 >= 5 -> passes
    // Row 4: prev passing = row 3 (value 10.0), |11-10|=1 < 5 -> filtered
    auto batch = make_table({1.0, 2.0, 3.0, 10.0, 11.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    auto& r = result.value();
    // Row 0 passes (first row), row 3 passes (big jump from row 0's value 1.0)
    // Rows 1, 2, 4 are filtered
    EXPECT_EQ(r.rows_passed, 2);
    EXPECT_EQ(r.rows_filtered, 3);

    // Verify the filtered batch has exactly 2 rows
    ASSERT_NE(r.filtered_batch, nullptr);
    EXPECT_EQ(r.filtered_batch->num_rows(), 2);

    // Verify outgoing state has last passing value = 10.0
    ASSERT_EQ(r.outgoing_states.size(), 1u);
    EXPECT_TRUE(r.outgoing_states[0].initialized);
    EXPECT_DOUBLE_EQ(r.outgoing_states[0].last_value.value(), 10.0);
}

// ===== Test 2: MonotoneIncrease with equal consecutive values -> filtered (strict) =====

TEST(ConstraintEdgeTest, MonotoneIncreaseRejectsEqualValues) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {c});
    // Row 0: no prev -> passes
    // Row 1: 2.0 > 1.0 -> passes
    // Row 2: 2.0 == 2.0 -> NOT strictly increasing -> filtered
    // Row 3: 3.0 > 2.0 (last passing) -> passes
    // Row 4: 3.0 == 3.0 -> filtered
    auto batch = make_table({1.0, 2.0, 2.0, 3.0, 3.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    auto& r = result.value();
    EXPECT_EQ(r.rows_passed, 3);  // rows 0, 1, 3
    EXPECT_EQ(r.rows_filtered, 2);  // rows 2, 4

    // Verify the filtered batch has correct values
    ASSERT_NE(r.filtered_batch, nullptr);
    EXPECT_EQ(r.filtered_batch->num_rows(), 3);
}

// ===== Test 3: MonotoneDecrease with equal consecutive values -> filtered (strict) =====

TEST(ConstraintEdgeTest, MonotoneDecreaseRejectsEqualValues) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneDecrease;

    InterRowEngine engine(s, {c});
    // Row 0: no prev -> passes
    // Row 1: 4.0 < 5.0 -> passes
    // Row 2: 4.0 == 4.0 -> NOT strictly decreasing -> filtered
    // Row 3: 3.0 < 4.0 (last passing) -> passes
    auto batch = make_table({5.0, 4.0, 4.0, 3.0, 3.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    auto& r = result.value();
    EXPECT_EQ(r.rows_passed, 3);  // rows 0, 1, 3
    EXPECT_EQ(r.rows_filtered, 2);  // rows 2, 4
}

// ===== Test 4: InterRow with single row -> first row always passes =====

TEST(ConstraintEdgeTest, SingleRowAlwaysPasses) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {c});
    auto batch = make_table({42.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.value().rows_passed, 1);
    EXPECT_EQ(result.value().rows_filtered, 0);
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);

    // Outgoing state should be initialized
    ASSERT_EQ(result.value().outgoing_states.size(), 1u);
    EXPECT_TRUE(result.value().outgoing_states[0].initialized);
    EXPECT_DOUBLE_EQ(result.value().outgoing_states[0].last_value.value(), 42.0);
}

// ===== Test 5: InterRow with incoming states from previous batch -> state continuity =====

TEST(ConstraintEdgeTest, CrossBatchStateContinuity) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {c});

    // First batch: strictly increasing
    auto batch1 = make_table({10.0, 20.0, 30.0});
    auto result1 = engine.execute_batch(batch1, {});
    ASSERT_TRUE(result1.ok());
    EXPECT_EQ(result1.value().rows_passed, 3);

    auto outgoing = result1.value().outgoing_states;
    ASSERT_EQ(outgoing.size(), 1u);
    EXPECT_TRUE(outgoing[0].initialized);
    EXPECT_DOUBLE_EQ(outgoing[0].last_value.value(), 30.0);

    // Second batch: values must be > 30.0
    // Row 0: 25.0 < 30.0 (from incoming state) -> filtered
    // Row 1: 35.0 > 30.0 -> passes (prev is incoming state since no passing row yet in this batch)
    // Row 2: 40.0 > 35.0 -> passes
    auto batch2 = make_table({25.0, 35.0, 40.0});
    auto result2 = engine.execute_batch(batch2, outgoing);
    ASSERT_TRUE(result2.ok());

    EXPECT_EQ(result2.value().rows_passed, 2);
    EXPECT_EQ(result2.value().rows_filtered, 1);
}

// ===== Test 6: DeltaMax=0.0 -> validation rejects (delta must be > 0) =====

TEST(ConstraintEdgeTest, DeltaMaxZeroRejectedByValidation) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 0.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 1.0, 1.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

// ===== Test 7: InterRow with very large delta -> all rows pass =====

TEST(ConstraintEdgeTest, VeryLargeDeltaMaxAllPass) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 10000.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({0.0, 500.0, 1000.0, 2000.0, 5000.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.value().rows_passed, 5);
    EXPECT_EQ(result.value().rows_filtered, 0);
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

// ===== Test 8: Single-window aggregate -> all data in one window =====

TEST(ConstraintEdgeTest, SingleWindowAggregate) {
    Schema s = make_agg_schema();
    AggregateConstraintDef c;
    c.constraint_name = "avg_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 100.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    // All timestamps within 1 hour -> single window
    auto batch = make_time_table({0, 100000, 200000, 300000}, {10.0, 20.0, 30.0, 40.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    // Single window with avg = 25.0 < 100.0 -> no violation
    auto& p2 = result.value().phase_two;
    EXPECT_EQ(p2.total_windows, 1);
    EXPECT_EQ(p2.windows_violated, 0);

    // Verify phase_one_output is the input batch
    EXPECT_EQ(result.value().phase_one_output->num_rows(), 4);
}

// ===== Test 9: Empty window (0 rows) -> should handle gracefully =====

TEST(ConstraintEdgeTest, EmptyInputToAggregate) {
    Schema s = make_agg_schema();
    AggregateConstraintDef c;
    c.constraint_name = "avg_temp";
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

    auto result = engine.execute(empty, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().phase_two.total_windows, 0);
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
    EXPECT_DOUBLE_EQ(result.value().total_exclusion_rate, 0.0);
}

// ===== Test 10: Aggregate kCount function matches actual row count =====

TEST(ConstraintEdgeTest, CountFunctionMatchesRowCount) {
    Schema s = make_agg_schema();
    AggregateEngine engine(s, {});

    auto batch = make_time_table({0, 1000, 2000, 3000, 4000},
                                 {1.0, 2.0, 3.0, 4.0, 5.0});

    AggregationWindow window;
    window.included_rows = {0, 1, 2, 3, 4};

    AggregateConstraintDef c;
    c.column_name = "temperature";
    c.function = AggregateFunction::kCount;

    auto result = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value(), 5.0);

    // Now test partial window subset
    AggregationWindow partial_window;
    partial_window.included_rows = {1, 2, 3};

    auto result2 = engine.compute_aggregate(batch, partial_window, c);
    ASSERT_TRUE(result2.ok());
    EXPECT_DOUBLE_EQ(result2.value(), 3.0);
}

// ===== Test 11: Aggregate with very small window interval (1 microsecond) =====

TEST(ConstraintEdgeTest, VerySmallWindowInterval) {
    Schema s = make_agg_schema();
    AggregateConstraintDef c;
    c.constraint_name = "count_per_us";
    c.column_name = "temperature";
    c.function = AggregateFunction::kCount;
    c.min_val = 1.0;  // At least 1 row per window
    c.window_interval_us = 1;  // 1 microsecond windows

    AggregateEngine engine(s, {c});
    // 10 rows, each 100 us apart -> 10 windows (each gets 1 row)
    std::vector<int64_t> timestamps;
    std::vector<double> temps;
    for (int i = 0; i < 10; ++i) {
        timestamps.push_back(i * 100);
        temps.push_back(static_cast<double>(i));
    }

    auto batch = make_time_table(timestamps, temps);
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    auto& p2 = result.value().phase_two;
    // With 1us windows and rows 100us apart, each row gets its own window
    EXPECT_GE(p2.total_windows, 5u);
    // Each window has exactly 1 row, count = 1 >= min_val = 1, so no violations
    EXPECT_EQ(p2.windows_violated, 0);
}

// ===== Test 12: Two-phase result: phase_one_output is correct =====

TEST(ConstraintEdgeTest, TwoPhaseOutputCorrect) {
    Schema s = make_agg_schema();
    AggregateConstraintDef c;
    c.constraint_name = "max_temp";
    c.column_name = "temperature";
    c.function = AggregateFunction::kMax;
    c.max_val = 50.0;
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 1000, 2000}, {10.0, 20.0, 30.0});
    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    auto& two_phase = result.value();
    // phase_one_output should be the input batch (current behavior)
    ASSERT_NE(two_phase.phase_one_output, nullptr);
    EXPECT_EQ(two_phase.phase_one_output->num_rows(), 3);

    // Verify the actual data in phase_one_output
    auto temp_col = two_phase.phase_one_output->column(1);
    auto double_arr = std::static_pointer_cast<arrow::DoubleArray>(temp_col->chunk(0));
    EXPECT_DOUBLE_EQ(double_arr->Value(0), 10.0);
    EXPECT_DOUBLE_EQ(double_arr->Value(1), 20.0);
    EXPECT_DOUBLE_EQ(double_arr->Value(2), 30.0);

    // Max = 30 < 50 -> no violations
    EXPECT_EQ(two_phase.phase_two.windows_violated, 0);
}

// ===== Test 13: Aggregate with data exactly at constraint boundary =====

TEST(ConstraintEdgeTest, AggregateExactBoundary) {
    Schema s = make_agg_schema();

    // Test AVG exactly equal to max_val -> should NOT be a violation
    AggregateConstraintDef c;
    c.constraint_name = "avg_exact";
    c.column_name = "temperature";
    c.function = AggregateFunction::kAvg;
    c.max_val = 25.0;  // avg will be exactly 25.0
    c.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {c});
    auto batch = make_time_table({0, 1000, 2000}, {20.0, 25.0, 30.0});
    // avg = (20 + 25 + 30) / 3 = 25.0 exactly

    auto result = engine.execute(batch, {});
    ASSERT_TRUE(result.ok());

    // avg == max_val -> agg_val > max_val is false -> no violation
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);

    // Also verify compute_aggregate directly
    AggregationWindow window;
    window.included_rows = {0, 1, 2};
    auto agg = engine.compute_aggregate(batch, window, c);
    ASSERT_TRUE(agg.ok());
    EXPECT_DOUBLE_EQ(agg.value(), 25.0);

    // Now test min boundary: avg exactly == min_val
    Schema s2 = make_agg_schema();
    AggregateConstraintDef c2;
    c2.constraint_name = "avg_min_exact";
    c2.column_name = "temperature";
    c2.function = AggregateFunction::kAvg;
    c2.min_val = 25.0;
    c2.window_interval_us = kOneHourUs;

    AggregateEngine engine2(s2, {c2});
    auto result2 = engine2.execute(batch, {});
    ASSERT_TRUE(result2.ok());

    // avg == min_val -> agg_val < min_val is false -> no violation
    EXPECT_EQ(result2.value().phase_two.windows_violated, 0);
}

// ===== Test 14: Classify with 50 value-range constraints =====

TEST(ConstraintEdgeTest, Classify50ValueRangeConstraints) {
    Schema s = make_ordered_schema();
    ConstraintSet cs;

    // Add 50 value-range constraint names
    for (int i = 0; i < 50; ++i) {
        cs.value_range_names.push_back("vr_constraint_" + std::to_string(i));
    }

    ConstraintClassifier classifier;
    auto result = classifier.classify(cs, s);
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.value().value_range_count, 50);
    EXPECT_EQ(result.value().inter_row_count, 0);
    EXPECT_EQ(result.value().aggregate_count, 0);
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kRowByRow);
    EXPECT_EQ(result.value().classifications.size(), 50u);

    // All should be phase one
    auto phase_one = result.value().phase_one_constraints();
    EXPECT_EQ(phase_one.size(), 50u);
    auto phase_two = result.value().phase_two_constraints();
    EXPECT_EQ(phase_two.size(), 0u);
}

// ===== Test 15: Route with data engine available vs unavailable =====

TEST(ConstraintEdgeTest, RouteDataEngineAvailableVsUnavailable) {
    Schema s = make_ordered_schema();

    // Create a classification with only value-range constraints
    ConstraintSet cs;
    cs.value_range_names.push_back("temp_range");
    ConstraintClassifier classifier;
    auto classification = classifier.classify(cs, s);
    ASSERT_TRUE(classification.ok());

    // Router with data engine UNavailable
    ExecutionRouter router_no_engine(false);
    auto decision_no_engine = router_no_engine.route(classification.value(), s);
    ASSERT_TRUE(decision_no_engine.ok());

    // Value-range only -> pure physics regardless of data engine
    EXPECT_EQ(decision_no_engine.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_FALSE(decision_no_engine.value().data_engine_available);
    EXPECT_EQ(std::string(ExecutionRouter::identity_for_path(DegradationPath::kPurePhysics)),
              "physics_sampler");

    // Router with data engine available
    ExecutionRouter router_with_engine(true);
    auto decision_with_engine = router_with_engine.route(classification.value(), s);
    ASSERT_TRUE(decision_with_engine.ok());

    // Value-range only + data engine available -> still pure physics
    EXPECT_EQ(decision_with_engine.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_TRUE(decision_with_engine.value().data_engine_available);

    // Now test with inter-row constraints -> different paths
    ConstraintSet cs_ir;
    cs_ir.value_range_names.push_back("temp_range");
    InterRowConstraintDef ir_def;
    ir_def.column_name = "temperature";
    ir_def.type = InterRowConstraintDef::Type::kDeltaMax;
    ir_def.delta_max = 5.0;
    cs_ir.inter_row_defs.push_back(ir_def);

    auto classification_ir = classifier.classify(cs_ir, s);
    ASSERT_TRUE(classification_ir.ok());

    // Without data engine -> pure physics (fallback)
    auto decision_ir_no = router_no_engine.route(classification_ir.value(), s);
    ASSERT_TRUE(decision_ir_no.ok());
    EXPECT_EQ(decision_ir_no.value().selected_path, DegradationPath::kPurePhysics);

    // With data engine -> post-filter path
    auto decision_ir_with = router_with_engine.route(classification_ir.value(), s);
    ASSERT_TRUE(decision_ir_with.ok());
    EXPECT_EQ(decision_ir_with.value().selected_path, DegradationPath::kPostFilter);
    EXPECT_EQ(std::string(ExecutionRouter::identity_for_path(DegradationPath::kPostFilter)),
              "post_filter_synthetic");
}
