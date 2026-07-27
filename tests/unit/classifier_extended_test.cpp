#include <gtest/gtest.h>
#include "engine/router/constraint_classifier.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

using namespace synthgen;
using namespace synthgen::engine::router;
using namespace synthgen::schema;

namespace {

Schema make_schema_with_order() {
    Schema s;
    s.type_name = "sensor";
    ColumnDef ts;
    ts.name = "timestamp";
    ts.type = DataType::kDatetime;
    ts.is_order = true;
    s.columns.push_back(ts);
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    s.columns.push_back(temp);
    return s;
}

ConstraintSet make_vr_only(int count) {
    ConstraintSet cs;
    for (int i = 0; i < count; ++i)
        cs.value_range_names.push_back("vr_" + std::to_string(i));
    return cs;
}

ConstraintSet make_ir_only(int count) {
    ConstraintSet cs;
    for (int i = 0; i < count; ++i) {
        engine::constraint::InterRowConstraintDef ird;
        ird.column_name = "temperature";
        ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
        ird.delta_max = static_cast<double>(i + 1);
        cs.inter_row_defs.push_back(ird);
    }
    return cs;
}

ConstraintSet make_agg_only(int count) {
    ConstraintSet cs;
    for (int i = 0; i < count; ++i) {
        engine::constraint::AggregateConstraintDef acd;
        acd.constraint_name = "agg_" + std::to_string(i);
        acd.column_name = "temperature";
        acd.function = engine::constraint::AggregateFunction::kAvg;
        acd.window_interval_us = 3600000000LL;
        cs.aggregate_defs.push_back(acd);
    }
    return cs;
}

}  // namespace

// ===== Derive Execution Mode Edge Cases =====

TEST(ClassifierExtended, AllZeroCountsIsRowByRow) {
    ConstraintClassifier cls;
    // All zero → row-by-row (default)
    EXPECT_EQ(cls.derive_execution_mode(0, 0, 0), ExecutionMode::kRowByRow);
}

TEST(ClassifierExtended, OnlyInterRowIsStatefulBatch) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(0, 5, 0), ExecutionMode::kStatefulBatch);
}

TEST(ClassifierExtended, OnlyAggregateIsTwoPhase) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(0, 0, 3), ExecutionMode::kTwoPhase);
}

TEST(ClassifierExtended, InterRowPlusAggregateIsTwoPhase) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(0, 2, 3), ExecutionMode::kTwoPhase);
}

TEST(ClassifierExtended, ValueRangePlusAggregateIsTwoPhase) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(5, 0, 1), ExecutionMode::kTwoPhase);
}

// ===== Classification result structure =====

TEST(ClassifierExtended, HasMethods) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    cs.value_range_names = {"vr1"};
    cs.inter_row_defs.push_back({});
    cs.inter_row_defs.back().column_name = "temperature";
    cs.inter_row_defs.back().type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    cs.inter_row_defs.back().delta_max = 5.0;
    cs.aggregate_defs.push_back({});
    cs.aggregate_defs.back().constraint_name = "agg1";
    cs.aggregate_defs.back().column_name = "temperature";
    cs.aggregate_defs.back().function = engine::constraint::AggregateFunction::kAvg;

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());

    EXPECT_GT(result.value().value_range_count, 0);
    EXPECT_TRUE(result.value().has_inter_row());
    EXPECT_TRUE(result.value().has_aggregate());
}

TEST(ClassifierExtended, HasMethodsOnlyVR) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    cs.value_range_names = {"vr1"};

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().value_range_count, 0);
    EXPECT_FALSE(result.value().has_inter_row());
    EXPECT_FALSE(result.value().has_aggregate());
}

// ===== Phase filtering with all three types =====

TEST(ClassifierExtended, PhaseFilteringAllThreeTypes) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    cs.value_range_names = {"vr1"};
    cs.inter_row_defs.push_back({});
    cs.inter_row_defs.back().column_name = "temperature";
    cs.inter_row_defs.back().type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    cs.inter_row_defs.back().delta_max = 5.0;
    cs.aggregate_defs.push_back({});
    cs.aggregate_defs.back().constraint_name = "agg1";
    cs.aggregate_defs.back().column_name = "temperature";
    cs.aggregate_defs.back().function = engine::constraint::AggregateFunction::kAvg;

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());

    auto p1 = result.value().phase_one_constraints();
    auto p2 = result.value().phase_two_constraints();

    // Phase one: value_range + inter_row
    bool has_vr = false, has_ir = false, has_agg = false;
    for (const auto& c : p1) {
        if (c.type == ConstraintType::kValueRange) has_vr = true;
        if (c.type == ConstraintType::kInterRow) has_ir = true;
    }
    for (const auto& c : p2) {
        if (c.type == ConstraintType::kAggregate) has_agg = true;
    }

    EXPECT_TRUE(has_vr);
    EXPECT_TRUE(has_ir);
    EXPECT_TRUE(has_agg);
}

// ===== Large constraint sets =====

TEST(ClassifierExtended, LargeValueRangeSet) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    auto cs = make_vr_only(100);
    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().value_range_count, 100);
    EXPECT_EQ(result.value().classifications.size(), 100u);
}

TEST(ClassifierExtended, LargeInterRowSet) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    auto cs = make_ir_only(50);
    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().inter_row_count, 50);
}

TEST(ClassifierExtended, LargeAggregateSet) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    auto cs = make_agg_only(50);
    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().aggregate_count, 50);
}

TEST(ClassifierExtended, MixedLargeSet) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    auto cs = make_vr_only(30);
    auto ir = make_ir_only(20);
    auto agg = make_agg_only(10);
    cs.inter_row_defs = ir.inter_row_defs;
    cs.aggregate_defs = agg.aggregate_defs;

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().value_range_count, 30);
    EXPECT_EQ(result.value().inter_row_count, 20);
    EXPECT_EQ(result.value().aggregate_count, 10);
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

// ===== Inter-row with MonotoneIncrease type =====

TEST(ClassifierExtended, InterRowMonotoneTypes) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kMonotoneIncrease;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
}

TEST(ClassifierExtended, InterRowMonotoneDecreaseType) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kMonotoneDecrease;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
}

TEST(ClassifierExtended, InterRowDeltaMinType) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMin;
    ird.delta_min = 1.0;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
}

// ===== Aggregate with different functions =====

TEST(ClassifierExtended, AggregateSumFunction) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "sum_test";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kSum;
    acd.window_interval_us = 3600000000LL;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

TEST(ClassifierExtended, AggregateMinFunction) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "min_test";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kMin;
    acd.window_interval_us = 3600000000LL;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

TEST(ClassifierExtended, AggregateMaxFunction) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "max_test";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kMax;
    acd.window_interval_us = 3600000000LL;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

TEST(ClassifierExtended, AggregateCountFunction) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "count_test";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kCount;
    acd.window_interval_us = 3600000000LL;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

// ===== Classification with single VR =====

TEST(ClassifierExtended, SingleValueRangeClassification) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"only_one"};

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().classifications.size(), 1u);
    EXPECT_EQ(result.value().classifications[0].constraint_name, "only_one");
    EXPECT_EQ(result.value().classifications[0].type, ConstraintType::kValueRange);
    EXPECT_EQ(result.value().classifications[0].phase, ExecutionPhase::kPhaseOne);
}

// ===== Repeated classification =====

TEST(ClassifierExtended, RepeatedClassificationConsistency) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();

    for (int i = 0; i < 10; ++i) {
        ConstraintSet cs;
        cs.value_range_names = {"vr1"};
        auto result = cls.classify(cs, s);
        ASSERT_TRUE(result.ok());
        EXPECT_EQ(result.value().execution_mode, ExecutionMode::kRowByRow);
    }
}
