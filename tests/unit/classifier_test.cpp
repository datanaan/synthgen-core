#include <gtest/gtest.h>
#include "engine/router/constraint_classifier.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

using namespace synthgen;
using namespace synthgen::engine::router;
using namespace synthgen::schema;

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

Schema make_schema_no_order() {
    Schema s;
    s.type_name = "no_order";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    s.columns.push_back(temp);
    return s;
}

Schema make_schema_order_non_datetime() {
    Schema s;
    s.type_name = "bad_order";
    ColumnDef id;
    id.name = "id";
    id.type = DataType::kInt;
    id.is_order = true;
    s.columns.push_back(id);
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    s.columns.push_back(temp);
    return s;
}

// ===== Derive Execution Mode =====

TEST(ClassifierTest, DeriveRowByRow) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(3, 0, 0), ExecutionMode::kRowByRow);
}

TEST(ClassifierTest, DeriveStatefulBatch) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(1, 1, 0), ExecutionMode::kStatefulBatch);
}

TEST(ClassifierTest, DeriveTwoPhase) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(1, 0, 1), ExecutionMode::kTwoPhase);
}

TEST(ClassifierTest, DeriveTwoPhaseWithInterRow) {
    ConstraintClassifier cls;
    EXPECT_EQ(cls.derive_execution_mode(1, 1, 1), ExecutionMode::kTwoPhase);
}

// ===== Classify: Value Range Only =====

TEST(ClassifierTest, ClassifyValueRangeOnly) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kRowByRow);
    EXPECT_EQ(result.value().value_range_count, 1);
    EXPECT_EQ(result.value().inter_row_count, 0);
    EXPECT_EQ(result.value().aggregate_count, 0);
    EXPECT_EQ(result.value().classifications.size(), 1u);
    EXPECT_EQ(result.value().classifications[0].type, ConstraintType::kValueRange);
    EXPECT_EQ(result.value().classifications[0].phase, ExecutionPhase::kPhaseOne);
}

TEST(ClassifierTest, ClassifyManyValueRanges) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"r1", "r2", "r3", "r4", "r5",
                            "r6", "r7", "r8", "r9", "r10"};

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().value_range_count, 10);
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kRowByRow);
}

// ===== Classify: Inter-Row =====

TEST(ClassifierTest, ClassifyInterRowOnly) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
    EXPECT_EQ(result.value().inter_row_count, 1);
    EXPECT_TRUE(result.value().has_inter_row());
}

TEST(ClassifierTest, ClassifyInterRowNoOrderColumn) {
    ConstraintClassifier cls;
    Schema s = make_schema_no_order();
    ConstraintSet cs;
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kOrderColumnRequired);
}

// ===== Classify: Aggregate =====

TEST(ClassifierTest, ClassifyAggregateOnly) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kAvg;
    acd.window_interval_us = 3600000000LL;
    acd.max_val = 40.0;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_EQ(result.value().aggregate_count, 1);
    EXPECT_TRUE(result.value().has_aggregate());
}

TEST(ClassifierTest, ClassifyAggregateNonDatetimeOrder) {
    ConstraintClassifier cls;
    Schema s = make_schema_order_non_datetime();
    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kAvg;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kTypeMismatch);
}

TEST(ClassifierTest, ClassifyAggregateNoOrderColumn) {
    ConstraintClassifier cls;
    Schema s = make_schema_no_order();
    ConstraintSet cs;
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kOrderColumnRequired);
}

// ===== Classify: Mixed =====

TEST(ClassifierTest, ClassifyValueRangeAndInterRow) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
    EXPECT_EQ(result.value().value_range_count, 1);
    EXPECT_EQ(result.value().inter_row_count, 1);
}

TEST(ClassifierTest, ClassifyValueRangeAndAggregate) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kAvg;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
}

TEST(ClassifierTest, ClassifyAllThree) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};
    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kAvg;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_EQ(result.value().value_range_count, 1);
    EXPECT_EQ(result.value().inter_row_count, 1);
    EXPECT_EQ(result.value().aggregate_count, 1);
    EXPECT_EQ(result.value().classifications.size(), 3u);
}

// ===== Phase Filtering =====

TEST(ClassifierTest, PhaseFiltering) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};
    engine::constraint::AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = engine::constraint::AggregateFunction::kAvg;
    cs.aggregate_defs.push_back(acd);

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());

    auto p1 = result.value().phase_one_constraints();
    auto p2 = result.value().phase_two_constraints();
    EXPECT_EQ(p1.size(), 1u);
    EXPECT_EQ(p2.size(), 1u);
    EXPECT_EQ(p1[0].type, ConstraintType::kValueRange);
    EXPECT_EQ(p2[0].type, ConstraintType::kAggregate);
}

// ===== Error: Empty =====

TEST(ClassifierTest, EmptyConstraints) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;

    auto result = cls.classify(cs, s);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===== Large Constraint Set =====

TEST(ClassifierTest, LargeConstraintSet) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;

    for (int i = 0; i < 25; ++i) {
        cs.value_range_names.push_back("r" + std::to_string(i));
    }
    for (int i = 0; i < 25; ++i) {
        engine::constraint::InterRowConstraintDef ird;
        ird.column_name = "temperature";
        ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
        ird.delta_max = static_cast<double>(i + 1);
        cs.inter_row_defs.push_back(ird);
    }

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().value_range_count, 25);
    EXPECT_EQ(result.value().inter_row_count, 25);
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kStatefulBatch);
    EXPECT_EQ(result.value().classifications.size(), 50u);
}

// ===== Idempotent =====

TEST(ClassifierTest, IdempotentClassification) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};

    auto r1 = cls.classify(cs, s);
    auto r2 = cls.classify(cs, s);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r1.value().execution_mode, r2.value().execution_mode);
    EXPECT_EQ(r1.value().classifications.size(), r2.value().classifications.size());
}

// ===== Trace =====

TEST(ClassifierTest, ProducesTraceSpan) {
    scaffold::SpanGuard::active_spans().clear();
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};

    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());

    bool found = false;
    for (const auto& sp : scaffold::SpanGuard::active_spans()) {
        if (sp.component == "classifier") found = true;
    }
    EXPECT_TRUE(found);
}

// ===== Explain =====

TEST(ClassifierTest, ExplainReturns) {
    ConstraintClassifier cls;
    Schema s = make_schema_with_order();
    ConstraintSet cs;
    cs.value_range_names = {"range1"};
    auto result = cls.classify(cs, s);
    ASSERT_TRUE(result.ok());
    auto info = cls.explain(result.value());
    // Just verify no crash
}
