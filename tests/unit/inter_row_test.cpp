#include <gtest/gtest.h>
#include "engine/constraint/inter_row_engine.h"
#include "schema/schema.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <cmath>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;

// Helper: create a schema with ORDER column
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
    vib.range_min = 0.0;
    vib.range_max = 10.0;
    s.columns.push_back(vib);
    return s;
}

// Helper: create Arrow table from column data
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

// ===== Error Tests =====

TEST(InterRowEngineTest, NoOrderColumn) {
    Schema s;
    s.type_name = "no_order";
    ColumnDef col;
    col.name = "temp";
    col.type = DataType::kFloat;
    s.columns.push_back(col);

    InterRowConstraintDef c;
    c.column_name = "temp";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kOrderColumnRequired);
}

TEST(InterRowEngineTest, UndefinedColumn) {
    Schema s = make_ordered_schema();

    InterRowConstraintDef c;
    c.column_name = "nonexistent";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedColumn);
}

TEST(InterRowEngineTest, TypeMismatch) {
    Schema s;
    s.type_name = "test";
    ColumnDef ts;
    ts.name = "timestamp";
    ts.type = DataType::kDatetime;
    ts.is_order = true;
    s.columns.push_back(ts);
    ColumnDef status;
    status.name = "status";
    status.type = DataType::kString;
    s.columns.push_back(status);

    InterRowConstraintDef c;
    c.column_name = "status";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    // Build a string column table is complex, just test error on validate
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kTypeMismatch);
}

TEST(InterRowEngineTest, InvalidDeltaMax) {
    Schema s = make_ordered_schema();

    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = -1.0;  // Invalid

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

TEST(InterRowEngineTest, InvalidDeltaMaxZero) {
    Schema s = make_ordered_schema();

    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 0.0;  // Invalid

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

TEST(InterRowEngineTest, InvalidDeltaMin) {
    Schema s = make_ordered_schema();

    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMin;
    c.delta_min = -5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

// ===== Empty Batch =====

TEST(InterRowEngineTest, EmptyBatch) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto empty_table = arrow::Table::MakeEmpty(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("temperature", arrow::float64()),
            arrow::field("vibration", arrow::float64())
        })).ValueOrDie();

    auto result = engine.execute_batch(empty_table, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 0);
    EXPECT_EQ(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, NullBatch) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto result = engine.execute_batch(nullptr, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 0);
}

// ===== Functional Tests =====

TEST(InterRowEngineTest, DeltaMaxPassAll) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 10.0;

    InterRowEngine engine(s, {c});
    // All differences < 10
    auto batch = make_table({1.0, 2.0, 3.0, 5.0, 8.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    // First row has no previous (no incoming state), so it passes
    // Remaining rows: diffs are 1,1,2,3 — all < 10
    EXPECT_GT(result.value().rows_passed, 0);
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

TEST(InterRowEngineTest, DeltaMaxFiltersSome) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 2.0;

    InterRowEngine engine(s, {c});
    // diffs: 1, 1, 5, 1 — 5 > 2, row 3 filtered
    auto batch = make_table({10.0, 11.0, 12.0, 17.0, 18.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, DeltaMinFiltersSmall) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMin;
    c.delta_min = 3.0;

    InterRowEngine engine(s, {c});
    // diffs: 1, 1, 4, 1 — only diff 4 passes delta_min
    auto batch = make_table({10.0, 11.0, 12.0, 16.0, 17.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, MonotoneIncrease) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {c});
    // Strictly increasing
    auto batch = make_table({1.0, 2.0, 3.0, 4.0, 5.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, MonotoneIncreaseFailsOnDecrease) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {c});
    // 3→2 is a decrease
    auto batch = make_table({1.0, 2.0, 3.0, 2.0, 5.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, MonotoneDecrease) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kMonotoneDecrease;

    InterRowEngine engine(s, {c});
    // Strictly decreasing
    auto batch = make_table({5.0, 4.0, 3.0, 2.0, 1.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, CrossBatchState) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});

    // First batch
    auto batch1 = make_table({10.0, 12.0});
    auto result1 = engine.execute_batch(batch1, {});
    ASSERT_TRUE(result1.ok());
    EXPECT_EQ(result1.value().rows_passed, 2);
    auto outgoing = result1.value().outgoing_states;

    // Use outgoing states as incoming for next batch
    // last passing value = 12.0, next value = 20.0 → diff = 8 > 5 → filtered
    auto batch2 = make_table({20.0, 21.0});
    auto result2 = engine.execute_batch(batch2, outgoing);
    ASSERT_TRUE(result2.ok());
    EXPECT_GT(result2.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, CrossBatchStatePass) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});

    auto batch1 = make_table({10.0, 12.0});
    auto result1 = engine.execute_batch(batch1, {});
    ASSERT_TRUE(result1.ok());
    auto outgoing = result1.value().outgoing_states;

    // last passing value = 12.0, next value = 14.0 → diff = 2 < 5 → pass
    auto batch2 = make_table({14.0, 16.0});
    auto result2 = engine.execute_batch(batch2, outgoing);
    ASSERT_TRUE(result2.ok());
    EXPECT_EQ(result2.value().filter_rate, 0.0);
}

// ===== Boundary Tests =====

TEST(InterRowEngineTest, SingleRow) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({42.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 1);  // Single row always passes
    EXPECT_EQ(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, TwoRows) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({10.0, 14.0});  // diff = 4 < 5
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 2);
}

TEST(InterRowEngineTest, ColdStartNoIncomingState) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({100.0, 101.0, 102.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    // First row has no incoming state → passes
    // diffs: 1, 1 → all < 5
    EXPECT_EQ(result.value().rows_passed, 3);
}

TEST(InterRowEngineTest, AllRowsFiltered) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 0.001;  // Very small

    InterRowEngine engine(s, {c});
    // Large jumps
    auto batch = make_table({0.0, 100.0, 200.0, 300.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    // First row passes (no prev), rest filtered
    EXPECT_GE(result.value().rows_filtered, 2);
}

TEST(InterRowEngineTest, VeryLargeDeltaMax) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = DBL_MAX;

    InterRowEngine engine(s, {c});
    auto batch = make_table({0.0, 1e10, 1e20});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

TEST(InterRowEngineTest, MultipleConstraints) {
    Schema s = make_ordered_schema();

    InterRowConstraintDef c1;
    c1.column_name = "temperature";
    c1.type = InterRowConstraintDef::Type::kDeltaMax;
    c1.delta_max = 5.0;

    InterRowConstraintDef c2;
    c2.column_name = "vibration";
    c2.type = InterRowConstraintDef::Type::kDeltaMax;
    c2.delta_max = 2.0;

    InterRowEngine engine(s, {c1, c2});
    // temp diffs: 1,1,1,1 → all pass; vib diffs: 10,0,0,0 → first diff fails
    auto batch = make_table({1.0, 2.0, 3.0, 4.0, 5.0}, {0.0, 10.0, 10.0, 10.0, 10.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowEngineTest, OutgoingStateCorrect) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({10.0, 12.0, 15.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    ASSERT_EQ(result.value().outgoing_states.size(), 1u);
    const auto& state = result.value().outgoing_states[0];
    EXPECT_TRUE(state.initialized);
    EXPECT_DOUBLE_EQ(state.last_value.value(), 15.0);
    EXPECT_EQ(state.column_name, "temperature");
}

// ===== Scaffold Tests =====

TEST(InterRowEngineTest, ProducesTraceSpan) {
    scaffold::SpanGuard::active_spans().clear();

    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    bool found = false;
    for (const auto& sp : scaffold::SpanGuard::active_spans()) {
        if (sp.component == "inter_row_engine" && sp.operation == "execute_batch") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(InterRowEngineTest, OrderColumnExposed) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    EXPECT_EQ(engine.order_column(), "timestamp");
}

TEST(InterRowEngineTest, ExplainReturns) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c;
    c.column_name = "temperature";
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = 5.0;

    InterRowEngine engine(s, {c});
    auto info = engine.explain();
    // Just check it doesn't crash
}
