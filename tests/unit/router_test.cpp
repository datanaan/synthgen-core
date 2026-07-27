#include <gtest/gtest.h>
#include "engine/router/execution_router.h"
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

ClassificationResult make_classification(int vr, int ir, int agg) {
    ClassificationResult r;
    r.value_range_count = vr;
    r.inter_row_count = ir;
    r.aggregate_count = agg;
    if (agg > 0) r.execution_mode = ExecutionMode::kTwoPhase;
    else if (ir > 0) r.execution_mode = ExecutionMode::kStatefulBatch;
    else r.execution_mode = ExecutionMode::kRowByRow;
    return r;
}

// ===== Identity Mapping =====

TEST(RouterTest, IdentityFullFunction) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kFullFunction),
                 "constraint_driven_synthetic");
}

TEST(RouterTest, IdentityPostFilter) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kPostFilter),
                 "post_filter_synthetic");
}

TEST(RouterTest, IdentityPurePhysics) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kPurePhysics),
                 "physics_sampler");
}

TEST(RouterTest, IdentityStatisticalGeneration) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kStatisticalGeneration),
                 "statistical_generator");
}

TEST(RouterTest, IdentityKDEPerturbation) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kKDEPerturbation),
                 "kde_perturbation_generator");
}

// ===== Pure Physics (No Data Engine) =====

TEST(RouterTest, ValueRangeOnly_NoDataEngine_PurePhysics) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(3, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_DOUBLE_EQ(result.value().estimated_exclusion_rate, 0.0);
}

TEST(RouterTest, InterRow_NoDataEngine_PurePhysics) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 1, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
}

TEST(RouterTest, Aggregate_NoDataEngine_PurePhysics) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 1);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
}

TEST(RouterTest, NoConstraints_NoDataEngine_PurePhysics) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
}

// ===== With Data Engine =====

TEST(RouterTest, AggregateWithDataEngine_FullFunction) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 1);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kFullFunction);
}

TEST(RouterTest, InterRowWithDataEngine_PostFilter) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 1, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPostFilter);
}

TEST(RouterTest, ValueRangeWithDataEngine_PurePhysics) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(2, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
}

TEST(RouterTest, NoConstraintsWithDataEngine_StatisticalGeneration) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kStatisticalGeneration);
}

// ===== Identity in Decision =====

TEST(RouterTest, DecisionHasIdentity) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().identity.identity, "physics_sampler");
    EXPECT_FALSE(result.value().identity.justification.empty());
    EXPECT_EQ(result.value().identity.path, DegradationPath::kPurePhysics);
}

TEST(RouterTest, IdentityToString) {
    IdentityDeclaration id;
    id.identity = "test";
    id.justification = "reason";
    id.path = DegradationPath::kPurePhysics;
    EXPECT_NE(id.to_string().find("test"), std::string::npos);
    EXPECT_NE(id.to_string().find("reason"), std::string::npos);
}

// ===== Decision Reason =====

TEST(RouterTest, DecisionHasReason) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().decision_reason.empty());
}

// ===== Data Engine Availability =====

TEST(RouterTest, DataEngineAvailable) {
    ExecutionRouter router(true);
    EXPECT_TRUE(router.is_data_engine_available());
}

TEST(RouterTest, DataEngineNotAvailable) {
    ExecutionRouter router(false);
    EXPECT_FALSE(router.is_data_engine_available());
}

// ===== Routing Decision Preserves Classification =====

TEST(RouterTest, ClassificationPreserved) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(2, 1, 1);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().classification.value_range_count, 2);
    EXPECT_EQ(result.value().classification.inter_row_count, 1);
    EXPECT_EQ(result.value().classification.aggregate_count, 1);
}

// ===== Trace =====

TEST(RouterTest, ProducesTraceSpan) {
    synthgen::scaffold::SpanGuard::active_spans().clear();
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());

    bool found = false;
    for (const auto& sp : synthgen::scaffold::SpanGuard::active_spans()) {
        if (sp.component == "router") found = true;
    }
    EXPECT_TRUE(found);
}

// ===== Explain =====

TEST(RouterTest, ExplainReturns) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);
    auto info = router.explain(cls);
    // Just verify no crash
}

// ===== All Three Constraint Types With Data Engine =====

TEST(RouterTest, AllThreeWithDataEngine_FullFunction) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 1, 1);

    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kFullFunction);
}
