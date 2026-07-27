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
#include <limits>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;

// Shared helpers
namespace {

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

InterRowConstraintDef make_delta_max(const std::string& col, double delta) {
    InterRowConstraintDef c;
    c.column_name = col;
    c.type = InterRowConstraintDef::Type::kDeltaMax;
    c.delta_max = delta;
    return c;
}

InterRowConstraintDef make_delta_min(const std::string& col, double delta) {
    InterRowConstraintDef c;
    c.column_name = col;
    c.type = InterRowConstraintDef::Type::kDeltaMin;
    c.delta_min = delta;
    return c;
}

InterRowConstraintDef make_mono_inc(const std::string& col) {
    InterRowConstraintDef c;
    c.column_name = col;
    c.type = InterRowConstraintDef::Type::kMonotoneIncrease;
    return c;
}

InterRowConstraintDef make_mono_dec(const std::string& col) {
    InterRowConstraintDef c;
    c.column_name = col;
    c.type = InterRowConstraintDef::Type::kMonotoneDecrease;
    return c;
}

}  // namespace

// ===== DeltaMax: Exact boundary =====

TEST(InterRowExtended, DeltaMaxAtBoundaryFails) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});
    // diff = 5.0 exactly → should fail (< is strict, not <=)
    auto batch = make_table({10.0, 15.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowExtended, DeltaMaxJustBelowBoundaryPasses) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});
    // diff = 4.999 → passes
    auto batch = make_table({10.0, 14.999});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

// ===== DeltaMin: Exact boundary and functional =====

TEST(InterRowExtended, DeltaMinZeroIsInvalid) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_min("temperature", 0.0)});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

TEST(InterRowExtended, DeltaMinAtBoundary) {
    Schema s = make_ordered_schema();
    // delta_min = 5.0, diff = 5.0 → should fail (> is strict)
    InterRowEngine engine(s, {make_delta_min("temperature", 5.0)});
    auto batch = make_table({10.0, 15.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowExtended, DeltaMinJustAboveBoundaryPasses) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_min("temperature", 5.0)});
    // diff = 5.001 → passes
    auto batch = make_table({10.0, 15.001});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 2);
}

// ===== Monotone: Equal values =====

TEST(InterRowExtended, MonotoneIncreaseEqualValuesFails) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_mono_inc("temperature")});
    // equal values: 5.0 → 5.0 is NOT increase
    auto batch = make_table({5.0, 5.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowExtended, MonotoneDecreaseEqualValuesFails) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_mono_dec("temperature")});
    auto batch = make_table({5.0, 5.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

// ===== Multiple constraint types combined =====

TEST(InterRowExtended, DeltaMaxAndMonotoneIncreaseConflict) {
    Schema s = make_ordered_schema();
    // DeltaMax = 2.0, MonotoneIncrease
    // Values: 10, 11 → pass both (diff=1<2, increasing)
    // Values: 10, 13 → pass delta but fail mono? no, 13>10, passes both. diff=3>2 fails delta
    InterRowEngine engine(s, {
        make_delta_max("temperature", 2.0),
        make_mono_inc("temperature")
    });
    // 10 → pass (no prev)
    // 13 → diff=3 > 2 → filtered
    auto batch = make_table({10.0, 13.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowExtended, TwoConstraintsOnDifferentColumns) {
    Schema s = make_ordered_schema();
    // temp delta_max=2, vib delta_max=1
    // temp: 10,11,12,13 — all diffs ≤ 2, but vib: 0,0.5,2,0 — diff 1.5>1 at row 3
    InterRowEngine engine(s, {
        make_delta_max("temperature", 2.0),
        make_delta_max("vibration", 1.0)
    });
    auto batch = make_table({10.0, 11.0, 12.0, 13.0}, {0.0, 0.5, 2.0, 0.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

TEST(InterRowExtended, AllConstraintsOnSameColumnAllFail) {
    Schema s = make_ordered_schema();
    // DeltaMax=2 AND MonotoneIncrease — decreasing values fail both
    InterRowEngine engine(s, {
        make_delta_max("temperature", 2.0),
        make_mono_inc("temperature")
    });
    auto batch = make_table({10.0, 5.0, 2.0});  // all decreasing
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GE(result.value().rows_filtered, 2);
}

// ===== State transition edge cases =====

TEST(InterRowExtended, EmptyIncomingStates) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});
    auto batch = make_table({10.0, 12.0, 14.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 3);
}

TEST(InterRowExtended, ThreeBatchStateChain) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});

    auto b1 = make_table({10.0, 12.0});
    auto r1 = engine.execute_batch(b1, {});
    ASSERT_TRUE(r1.ok());

    auto b2 = make_table({14.0, 16.0});
    auto r2 = engine.execute_batch(b2, r1.value().outgoing_states);
    ASSERT_TRUE(r2.ok());

    auto b3 = make_table({18.0, 22.0});  // diff 22-18=4 < 5, all pass
    auto r3 = engine.execute_batch(b3, r2.value().outgoing_states);
    ASSERT_TRUE(r3.ok());
    EXPECT_DOUBLE_EQ(r3.value().filter_rate, 0.0);
}

TEST(InterRowExtended, ThreeBatchStateViolation) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 3.0)});

    auto b1 = make_table({10.0, 11.0});
    auto r1 = engine.execute_batch(b1, {});
    ASSERT_TRUE(r1.ok());

    auto b2 = make_table({12.0, 13.0});
    auto r2 = engine.execute_batch(b2, r1.value().outgoing_states);
    ASSERT_TRUE(r2.ok());

    // last = 13, next = 20 → diff=7 > 3 → filtered
    auto b3 = make_table({20.0, 21.0});
    auto r3 = engine.execute_batch(b3, r2.value().outgoing_states);
    ASSERT_TRUE(r3.ok());
    EXPECT_GT(r3.value().rows_filtered, 0);
}

// ===== Large batch =====

TEST(InterRowExtended, LargeBatch) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 100.0)});

    std::vector<double> temps;
    for (int i = 0; i < 10000; ++i) {
        temps.push_back(static_cast<double>(i));
    }
    auto batch = make_table(temps);
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 10000);
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

TEST(InterRowExtended, LargeBatchAllFiltered) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 0.001)});

    std::vector<double> temps;
    for (int i = 0; i < 10000; ++i) {
        temps.push_back(static_cast<double>(i * 100));
    }
    auto batch = make_table(temps);
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    // Only first row passes
    EXPECT_EQ(result.value().rows_passed, 1);
    EXPECT_EQ(result.value().rows_filtered, 9999);
}

// ===== Alternating pass/fail pattern =====

TEST(InterRowExtended, AlternatingPassFail) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 3.0)});
    // 10 → pass (no prev)
    // 12 → pass (diff=2 < 3)
    // 20 → fail (diff=8 > 3)
    // 21 → pass (prev was last passing = 12, diff=9 > 3? No — prev passing = 12, 21-12=9 > 3 → fail)
    auto batch = make_table({10.0, 12.0, 20.0, 21.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

// ===== Filter rate precision =====

TEST(InterRowExtended, FilterRateCalculation) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 1.0)});
    // diffs: 100, 0.5, 100, 0.5
    // row 0: pass (no prev)
    // row 1: diff=100 → fail
    // row 2: diff=0.5 → pass
    // row 3: diff=100 → fail
    // row 4: diff=0.5 → pass
    auto batch = make_table({0.0, 100.0, 100.5, 200.5, 201.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
    EXPECT_GT(result.value().filter_rate, 0.0);
    EXPECT_LT(result.value().filter_rate, 1.0);
}

// ===== Outgoing state with multiple constraints =====

TEST(InterRowExtended, OutgoingStatesMultipleConstraints) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {
        make_delta_max("temperature", 10.0),
        make_delta_max("vibration", 5.0)
    });
    auto batch = make_table({10.0, 12.0}, {1.0, 3.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());

    ASSERT_EQ(result.value().outgoing_states.size(), 2u);
    EXPECT_EQ(result.value().outgoing_states[0].column_name, "temperature");
    EXPECT_EQ(result.value().outgoing_states[1].column_name, "vibration");
    EXPECT_TRUE(result.value().outgoing_states[0].initialized);
    EXPECT_TRUE(result.value().outgoing_states[1].initialized);
}

// ===== NaN and Inf values =====

TEST(InterRowExtended, NaNValueInData) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});
    auto batch = make_table({10.0, std::numeric_limits<double>::quiet_NaN()});
    auto result = engine.execute_batch(batch, {});
    // NaN comparisons should not crash; behavior is implementation-defined
    // Just verify it doesn't crash
    ASSERT_TRUE(result.ok());
}

TEST(InterRowExtended, InfValueInData) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 5.0)});
    auto batch = make_table({10.0, std::numeric_limits<double>::infinity()});
    auto result = engine.execute_batch(batch, {});
    // Inf - 10 = Inf, which is > 5 → should be filtered
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_filtered, 0);
}

// ===== Negative delta_min validation =====

TEST(InterRowExtended, DeltaMinNegativeInvalid) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_min("temperature", -1.0)});
    auto batch = make_table({1.0, 5.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidDelta);
}

// ===== Multiple undefined columns =====

TEST(InterRowExtended, MultipleUndefinedColumns) {
    Schema s = make_ordered_schema();
    InterRowConstraintDef c1, c2;
    c1.column_name = "nonexistent_a";
    c1.type = InterRowConstraintDef::Type::kDeltaMax;
    c1.delta_max = 5.0;
    c2.column_name = "nonexistent_b";
    c2.type = InterRowConstraintDef::Type::kDeltaMax;
    c2.delta_max = 5.0;

    InterRowEngine engine(s, {c1, c2});
    auto batch = make_table({1.0, 2.0});
    auto result = engine.execute_batch(batch, {});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedColumn);
}

// ===== Verify state last_value tracking =====

TEST(InterRowExtended, OutgoingStateLastValueCorrect) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_delta_max("temperature", 20.0)});  // delta=20, all diffs < 20
    // Values: 10, 20, 30 → all pass (diffs: 10, 10 → all < 20)
    auto batch = make_table({10.0, 20.0, 30.0});
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().outgoing_states.size(), 1u);
    EXPECT_DOUBLE_EQ(result.value().outgoing_states[0].last_value.value(), 30.0);
}

// ===== Monotone with large dataset =====

TEST(InterRowExtended, MonotoneIncreaseLargeDataset) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_mono_inc("temperature")});

    std::vector<double> temps;
    for (int i = 0; i < 1000; ++i) temps.push_back(static_cast<double>(i));

    auto batch = make_table(temps);
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}

TEST(InterRowExtended, MonotoneDecreaseLargeDataset) {
    Schema s = make_ordered_schema();
    InterRowEngine engine(s, {make_mono_dec("temperature")});

    std::vector<double> temps;
    for (int i = 999; i >= 0; --i) temps.push_back(static_cast<double>(i));

    auto batch = make_table(temps);
    auto result = engine.execute_batch(batch, {});
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().filter_rate, 0.0);
}
