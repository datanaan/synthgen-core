#include <gtest/gtest.h>
#include "engine/router/execution_router.h"
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

}  // namespace

// ===== All constraint types without data engine =====

TEST(RouterExtended, AllThreeTypesNoDataEngine_PurePhysics) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(5, 3, 2);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPurePhysics);
}

// ===== Volume ratio in decision =====

TEST(RouterExtended, DecisionHasVolumeRatio) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(2, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    // VolumeRatioInfo has default ratio = 1.0
    EXPECT_DOUBLE_EQ(result.value().volume_ratio.ratio, 1.0);
}

// ===== Decision reason populated =====

TEST(RouterExtended, FullFunctionReason) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 1);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().decision_reason.empty());
}

TEST(RouterExtended, PostFilterReason) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 1, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().decision_reason.empty());
}

TEST(RouterExtended, PurePhysicsReason) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().decision_reason.empty());
}

TEST(RouterExtended, StatisticalGenerationReason) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().decision_reason.empty());
}

// ===== Estimated exclusion rate =====

TEST(RouterExtended, PurePhysicsExclusionRateZero) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(2, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().estimated_exclusion_rate, 0.0);
}

TEST(RouterExtended, DataEngineAvailableInDecision) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().data_engine_available);
}

TEST(RouterExtended, DataEngineNotAvailableInDecision) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().data_engine_available);
}

// ===== Identity for all paths =====

TEST(RouterExtended, IdentityForPathFullFunction) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kFullFunction),
                 "constraint_driven_synthetic");
}

TEST(RouterExtended, IdentityForPathPostFilter) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kPostFilter),
                 "post_filter_synthetic");
}

TEST(RouterExtended, IdentityForPathPurePhysics) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kPurePhysics),
                 "physics_sampler");
}

TEST(RouterExtended, IdentityForPathStatistical) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kStatisticalGeneration),
                 "statistical_generator");
}

TEST(RouterExtended, IdentityForPathKDE) {
    EXPECT_STREQ(ExecutionRouter::identity_for_path(DegradationPath::kKDEPerturbation),
                 "kde_perturbation_generator");
}

// ===== Identity in decision for each path =====

TEST(RouterExtended, FullFunctionIdentity) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 1);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().identity.path, DegradationPath::kFullFunction);
    EXPECT_FALSE(result.value().identity.identity.empty());
    EXPECT_FALSE(result.value().identity.justification.empty());
}

TEST(RouterExtended, PostFilterIdentity) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 1, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().identity.path, DegradationPath::kPostFilter);
}

TEST(RouterExtended, StatisticalIdentity) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().identity.path, DegradationPath::kStatisticalGeneration);
    EXPECT_EQ(result.value().identity.identity, "statistical_generator");
}

// ===== IdentityDeclaration to_string =====

TEST(RouterExtended, IdentityDeclarationToString) {
    IdentityDeclaration id;
    id.identity = "test_identity";
    id.justification = "test_reason";
    id.path = DegradationPath::kPurePhysics;

    auto str = id.to_string();
    EXPECT_NE(str.find("test_identity"), std::string::npos);
    EXPECT_NE(str.find("test_reason"), std::string::npos);
}

// ===== Routing with various classification combinations =====

TEST(RouterExtended, OnlyInterRowWithDataEngine_PostFilter) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 3, 0);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kPostFilter);
}

TEST(RouterExtended, OnlyAggregateWithDataEngine_FullFunction) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 0, 2);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kFullFunction);
}

TEST(RouterExtended, VRAndAggregateWithDataEngine_FullFunction) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(2, 0, 1);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kFullFunction);
}

TEST(RouterExtended, InterRowAndAggregateWithDataEngine_FullFunction) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(0, 1, 1);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().selected_path, DegradationPath::kFullFunction);
}

// ===== Classification preservation =====

TEST(RouterExtended, ClassificationCountsPreserved) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(5, 3, 2);
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().classification.value_range_count, 5);
    EXPECT_EQ(result.value().classification.inter_row_count, 3);
    EXPECT_EQ(result.value().classification.aggregate_count, 2);
}

TEST(RouterExtended, ExecutionModePreserved) {
    ExecutionRouter router(true);
    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 1);
    cls.execution_mode = ExecutionMode::kTwoPhase;
    auto result = router.route(cls, s);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().classification.execution_mode, ExecutionMode::kTwoPhase);
}

// ===== Multiple routers independent =====

TEST(RouterExtended, MultipleRoutersIndependent) {
    ExecutionRouter r1(false);
    ExecutionRouter r2(true);
    EXPECT_FALSE(r1.is_data_engine_available());
    EXPECT_TRUE(r2.is_data_engine_available());

    Schema s = make_schema_with_order();
    auto cls = make_classification(1, 0, 0);

    auto res1 = r1.route(cls, s);
    auto res2 = r2.route(cls, s);
    ASSERT_TRUE(res1.ok());
    ASSERT_TRUE(res2.ok());
    EXPECT_EQ(res1.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_EQ(res2.value().selected_path, DegradationPath::kPurePhysics);
}

// ===== Explain =====

TEST(RouterExtended, ExplainWithClassification) {
    ExecutionRouter router(false);
    Schema s = make_schema_with_order();
    auto cls = make_classification(3, 0, 0);
    auto info = router.explain(cls);
    // Just verify no crash and returns something
}

// ===== VolumeRatioInfo structure =====

TEST(RouterExtended, VolumeRatioInfoDefaults) {
    VolumeRatioInfo vri;
    EXPECT_DOUBLE_EQ(vri.constraint_volume, 0.0);
    EXPECT_DOUBLE_EQ(vri.data_distribution_volume, 0.0);
    EXPECT_DOUBLE_EQ(vri.ratio, 1.0);
    EXPECT_TRUE(vri.estimated);
}
