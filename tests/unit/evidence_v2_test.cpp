#include <gtest/gtest.h>
#include "common/types.h"
#include "engine/evidence/evidence_package_v2.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/router/execution_router.h"
#include "engine/router/constraint_classifier.h"
#include "engine/postfilter/post_filter.h"
#include "schema/schema.h"

using namespace synthgen;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::router;
using namespace synthgen::engine::postfilter;
using namespace synthgen::schema;

Schema make_test_schema() {
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
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    return s;
}

ClassificationResult make_cls(int vr, int ir, int agg) {
    ClassificationResult r;
    r.value_range_count = vr;
    r.inter_row_count = ir;
    r.aggregate_count = agg;
    return r;
}

// ===== Structure Tests =====

TEST(EvidencePackageV2Test, DefaultValues) {
    EvidencePackageV2 pkg;
    EXPECT_EQ(pkg.schema_version, "v2");
    EXPECT_EQ(pkg.audit_immutability, "verified");
    EXPECT_FALSE(pkg.statistical_fidelity.available);
    EXPECT_EQ(pkg.constraint_type_breakdown.value_range_count, 0);
    EXPECT_EQ(pkg.constraint_type_breakdown.inter_row_count, 0);
    EXPECT_EQ(pkg.constraint_type_breakdown.aggregate_count, 0);
}

// ===== Builder Tests =====

TEST(EvidencePackageV2Test, BuildBasic) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(false);
    auto cls = make_cls(2, 0, 0);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    pfr.pre_filter_rows = 100;
    pfr.post_filter_rows = 100;

    auto result = builder.build(100, 0.0, "physics_guaranteed",
                                decision.value(), cls, pfr, s);
    ASSERT_TRUE(result.ok());

    const auto& pkg = result.value();
    EXPECT_EQ(pkg.schema_version, "v2");
    EXPECT_EQ(pkg.row_count, 100);
    EXPECT_DOUBLE_EQ(pkg.exclusion_rate, 0.0);
    EXPECT_EQ(pkg.audit_immutability, "verified");
    EXPECT_EQ(pkg.constraint_type_breakdown.value_range_count, 2);
    EXPECT_FALSE(pkg.generator_identity.identity.empty());
}

TEST(EvidencePackageV2Test, BuildWithInterRow) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(false);
    auto cls = make_cls(1, 1, 0);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    auto result = builder.build(50, 0.2, "limited_fidelity",
                                decision.value(), cls, pfr, s);
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.value().constraint_type_breakdown.inter_row_count, 1);
    EXPECT_EQ(result.value().data_grade, "limited_fidelity");
}

TEST(EvidencePackageV2Test, BuildWithAggregate) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(true);
    auto cls = make_cls(1, 0, 1);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    pfr.actual_exclusion_rate = 0.15;
    pfr.pre_filter_rows = 100;
    pfr.post_filter_rows = 85;

    auto result = builder.build(85, 0.15, "statistics_guaranteed",
                                decision.value(), cls, pfr, s);
    ASSERT_TRUE(result.ok());

    EXPECT_EQ(result.value().constraint_type_breakdown.aggregate_count, 1);
    EXPECT_TRUE(result.value().post_filter_info.was_post_filtered);
    EXPECT_EQ(result.value().post_filter_info.pre_filter_rows, 100);
    EXPECT_EQ(result.value().post_filter_info.post_filter_rows, 85);
}

// ===== JSON Serialization =====

TEST(EvidencePackageV2Test, JsonRoundTrip) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(false);
    auto cls = make_cls(1, 0, 0);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    auto pkg_result = builder.build(100, 0.0, "physics_guaranteed",
                                     decision.value(), cls, pfr, s);
    ASSERT_TRUE(pkg_result.ok());

    auto json_result = builder.to_json(pkg_result.value());
    ASSERT_TRUE(json_result.ok());

    EXPECT_NE(json_result.value().find("\"v2\""), std::string::npos);
    EXPECT_NE(json_result.value().find("\"verified\""), std::string::npos);
    EXPECT_NE(json_result.value().find("\"constraint_type_breakdown\""), std::string::npos);
    EXPECT_NE(json_result.value().find("\"generator_identity\""), std::string::npos);
    EXPECT_NE(json_result.value().find("\"post_filter_info\""), std::string::npos);

    // Round trip
    auto parse_result = builder.from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok());

    EXPECT_EQ(parse_result.value().schema_version, "v2");
    EXPECT_EQ(parse_result.value().audit_immutability, "verified");
    EXPECT_EQ(parse_result.value().row_count, 100);
}

TEST(EvidencePackageV2Test, JsonInvalidFails) {
    EvidencePackageV2Builder builder;
    auto result = builder.from_json("{{{invalid");
    EXPECT_FALSE(result.ok());
}

// ===== Constraint Type Breakdown =====

TEST(EvidencePackageV2Test, ConstraintTypeBreakdown) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(true);
    auto cls = make_cls(3, 2, 1);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    auto pkg = builder.build(50, 0.1, "statistics_guaranteed",
                              decision.value(), cls, pfr, s);
    ASSERT_TRUE(pkg.ok());

    EXPECT_EQ(pkg.value().constraint_type_breakdown.value_range_count, 3);
    EXPECT_EQ(pkg.value().constraint_type_breakdown.inter_row_count, 2);
    EXPECT_EQ(pkg.value().constraint_type_breakdown.aggregate_count, 1);

    // JSON contains breakdown
    auto json = builder.to_json(pkg.value());
    ASSERT_TRUE(json.ok());
    EXPECT_NE(json.value().find("\"value_range\":3"), std::string::npos);
    EXPECT_NE(json.value().find("\"inter_row\":2"), std::string::npos);
    EXPECT_NE(json.value().find("\"aggregate\":1"), std::string::npos);
}

// ===== Statistical Fidelity =====

TEST(EvidencePackageV2Test, StatisticalFidelityDefaultFalse) {
    EvidencePackageV2 pkg;
    EXPECT_FALSE(pkg.statistical_fidelity.available);
}

// ===== Generator Identity =====

TEST(EvidencePackageV2Test, GeneratorIdentityInJson) {
    EvidencePackageV2Builder builder;
    Schema s = make_test_schema();

    ExecutionRouter router(false);
    auto cls = make_cls(1, 0, 0);
    auto decision = router.route(cls, s);
    ASSERT_TRUE(decision.ok());

    PostFilterResult pfr;
    auto pkg = builder.build(100, 0.0, "physics_guaranteed",
                              decision.value(), cls, pfr, s);
    ASSERT_TRUE(pkg.ok());

    auto json = builder.to_json(pkg.value());
    ASSERT_TRUE(json.ok());
    EXPECT_NE(json.value().find("physics_sampler"), std::string::npos);
}
