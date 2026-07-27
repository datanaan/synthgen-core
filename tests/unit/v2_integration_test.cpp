#include <gtest/gtest.h>
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "storage/audit/audit_log.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>

using namespace synthgen;
using namespace synthgen::engine::router;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage::audit;
using namespace synthgen::schema;

namespace {

Schema make_sensor_schema() {
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

std::shared_ptr<arrow::Table> make_data_table(
    const std::vector<int64_t>& timestamps,
    const std::vector<double>& temps,
    const std::vector<double>& vibs = {}) {

    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder temp_builder;
    arrow::DoubleBuilder vib_builder;
    for (auto t : timestamps) ts_builder.Append(t);
    for (auto v : temps) temp_builder.Append(v);
    if (vibs.empty()) {
        for (size_t i = 0; i < temps.size(); ++i) vib_builder.Append(0.0);
    } else {
        for (auto v : vibs) vib_builder.Append(v);
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

constexpr int64_t kOneHourUs = 3600000000LL;

}  // namespace

// ===== Path 1: kPurePhysics — value-range only, no data engine =====

TEST(V2Integration, PurePhysicsPath) {
    Schema s = make_sensor_schema();

    // Classify: value-range only
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range", "vib_range"};
    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok());
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kRowByRow);

    // Route: no data engine → pure physics
    ExecutionRouter router(false);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_EQ(routing.value().identity.identity, "physics_sampler");

    // Post-filter
    auto table = make_data_table({0, 100, 200}, {25.0, 30.0, 35.0});
    PostFilter pf;
    auto pf_result = pf.execute(table, 3);
    ASSERT_TRUE(pf_result.ok());

    // Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        3, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok());
    EXPECT_EQ(ep_result.value().schema_version, "v2");
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.value_range_count, 2);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 0);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.aggregate_count, 0);

    // Audit trail
    AuditLog audit;
    audit.create_genesis();
    audit.append("generate", "physics_sampler", {{"rows", "3"}, {"path", "pure_physics"}});
    EXPECT_EQ(audit.record_count(), 2);
}

// ===== Path 2: kPostFilter — inter-row constraints, data engine =====

TEST(V2Integration, PostFilterPath) {
    Schema s = make_sensor_schema();

    // Classify: inter-row + value-range
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);

    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok());
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kStatefulBatch);

    // Route: data engine → post-filter
    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kPostFilter);

    // Execute inter-row
    InterRowEngine ir_engine(s, {ird});
    auto table = make_data_table({0, 100, 200, 300, 400},
                                  {10.0, 12.0, 14.0, 16.0, 18.0});
    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok());
    EXPECT_EQ(ir_result.value().rows_passed, 5);

    // Post-filter the inter-row output
    PostFilter pf;
    auto pf_result = pf.execute(ir_result.value().filtered_batch, 5);
    ASSERT_TRUE(pf_result.ok());

    // Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok());
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 1);
}

// ===== Path 3: kFullFunction — all constraint types, data engine =====

TEST(V2Integration, FullFunctionPath) {
    Schema s = make_sensor_schema();

    // Classify: all three types
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 20.0;
    cs.inter_row_defs.push_back(ird);
    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 50.0;
    acd.window_interval_us = kOneHourUs;
    cs.aggregate_defs.push_back(acd);

    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok());
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kTwoPhase);

    // Route: data engine → full function
    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kFullFunction);

    // Execute aggregate (two-phase)
    AggregateEngine agg_engine(s, {acd});
    auto table = make_data_table(
        {0, 100, 200, kOneHourUs, kOneHourUs + 100},
        {20.0, 25.0, 30.0, 22.0, 28.0});
    auto agg_result = agg_engine.execute(table, {});
    ASSERT_TRUE(agg_result.ok());

    // Post-filter
    PostFilter pf;
    auto pf_result = pf.execute(agg_result.value().phase_one_output, 5);
    ASSERT_TRUE(pf_result.ok());

    // Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok());
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.value_range_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.aggregate_count, 1);

    // Audit trail
    AuditLog audit;
    audit.create_genesis();
    audit.append("generate", "constraint_driven_synthetic",
                  {{"path", "full_function"}, {"rows", "5"}});
    EXPECT_EQ(audit.record_count(), 2);
    EXPECT_TRUE(audit.verify_chain().ok());
}

// ===== Path 4: kStatisticalGeneration — no constraints, data engine =====

TEST(V2Integration, StatisticalGenerationPath) {
    Schema s = make_sensor_schema();

    // Classify: no constraints → error
    ConstraintClassifier classifier;
    ConstraintSet cs;
    auto cls_result = classifier.classify(cs, s);
    // Empty constraints is an error
    EXPECT_FALSE(cls_result.ok());
}

// ===== Classifier → Router → Evidence v2 (pure physics) =====

TEST(V2Integration, ClassifierRouterEvidenceChain) {
    Schema s = make_sensor_schema();

    for (int vr = 0; vr <= 2; ++vr) {
        for (int ir = 0; ir <= 1; ++ir) {
            for (int agg = 0; agg <= 1; ++agg) {
                if (vr == 0 && ir == 0 && agg == 0) continue;

                ConstraintSet cs;
                for (int i = 0; i < vr; ++i)
                    cs.value_range_names.push_back("vr_" + std::to_string(i));
                for (int i = 0; i < ir; ++i) {
                    InterRowConstraintDef ird;
                    ird.column_name = "temperature";
                    ird.type = InterRowConstraintDef::Type::kDeltaMax;
                    ird.delta_max = 10.0;
                    cs.inter_row_defs.push_back(ird);
                }
                for (int i = 0; i < agg; ++i) {
                    AggregateConstraintDef acd;
                    acd.constraint_name = "agg_" + std::to_string(i);
                    acd.column_name = "temperature";
                    acd.function = AggregateFunction::kAvg;
                    acd.max_val = 50.0;
                    acd.window_interval_us = kOneHourUs;
                    cs.aggregate_defs.push_back(acd);
                }

                ConstraintClassifier classifier;
                auto cls = classifier.classify(cs, s);
                ASSERT_TRUE(cls.ok()) << "Classify failed for vr=" << vr << " ir=" << ir << " agg=" << agg;

                ExecutionRouter router(false);
                auto routing = router.route(cls.value(), s);
                ASSERT_TRUE(routing.ok()) << "Route failed for vr=" << vr << " ir=" << ir << " agg=" << agg;

                // Should always be pure physics without data engine
                EXPECT_EQ(routing.value().selected_path, DegradationPath::kPurePhysics);
            }
        }
    }
}

// ===== Audit log throughout pipeline =====

TEST(V2Integration, AuditLogFullPipeline) {
    AuditLog audit;
    audit.create_genesis();

    Schema s = make_sensor_schema();

    // Step 1: Classify
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    auto cls = classifier.classify(cs, s);
    ASSERT_TRUE(cls.ok());
    audit.append("classify", "constraint_classifier");

    // Step 2: Route
    ExecutionRouter router(false);
    auto routing = router.route(cls.value(), s);
    ASSERT_TRUE(routing.ok());
    audit.append("route", "execution_router");

    // Step 3: Generate (simulated)
    auto table = make_data_table({0, 100, 200}, {25.0, 30.0, 35.0});
    audit.append("generate", "physics_sampler", {{"rows", "3"}});

    // Step 4: Post-filter
    PostFilter pf;
    auto pf_result = pf.execute(table, 3);
    ASSERT_TRUE(pf_result.ok());
    audit.append("post_filter", "post_filter_engine");

    // Step 5: Build evidence
    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(3, 0.0, "physics_guaranteed",
                                routing.value(), cls.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok());
    audit.append("evidence_build", "evidence_builder");

    // Verify audit chain
    EXPECT_EQ(audit.record_count(), 6);  // genesis + 5 steps
    auto verify = audit.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());

    // Daily verification
    auto report = audit.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 6);
}

// ===== Cross-batch inter-row → aggregate =====

TEST(V2Integration, InterRowThenAggregate) {
    Schema s = make_sensor_schema();

    // Step 1: Inter-row filtering
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 10.0;

    InterRowEngine ir_engine(s, {ird});
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800},
        {20.0, 25.0, 30.0, 35.0, 40.0, 45.0, 50.0, 55.0, 60.0});
    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok());

    // Step 2: Aggregate on filtered output
    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 100.0;
    acd.window_interval_us = kOneHourUs;

    AggregateEngine agg_engine(s, {acd});
    auto agg_result = agg_engine.execute(ir_result.value().filtered_batch, {});
    ASSERT_TRUE(agg_result.ok());
}

// ===== PostFilter + EvidencePackage with exclusion =====

TEST(V2Integration, PostFilterEvidenceWithExclusion) {
    Schema s = make_sensor_schema();

    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    auto cls = classifier.classify(cs, s);
    ASSERT_TRUE(cls.ok());

    ExecutionRouter router(true);
    auto routing = router.route(cls.value(), s);
    ASSERT_TRUE(routing.ok());

    // Simulate post-filter with exclusion
    PostFilter pf;
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {25.0, 30.0, 35.0, 100.0, 40.0, 45.0, 200.0, 50.0, 55.0, 60.0});
    auto pf_result = pf.execute(table, 5);
    ASSERT_TRUE(pf_result.ok());

    // Build evidence
    double exclusion_rate = pf_result.value().actual_exclusion_rate;
    std::string grade = PostFilter::data_grade_for_band(pf_result.value().rate_band);

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        static_cast<int64_t>(pf_result.value().post_filter_rows),
        exclusion_rate, grade,
        routing.value(), cls.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok());

    // Verify JSON round-trip
    auto json = ep_builder.to_json(ep.value());
    ASSERT_TRUE(json.ok());
    auto parsed = ep_builder.from_json(json.value());
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().schema_version, "v2");
}
