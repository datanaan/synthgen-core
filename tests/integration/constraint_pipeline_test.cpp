// Task 4: Constraint Pipeline Integration Tests
// Tests the full constraint pipeline: Classify -> Route -> Generate -> Validate -> PostFilter -> EvidenceV2

#include <gtest/gtest.h>
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/constraint/value_range_validator.h"
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

// ===== Test 1: ValueRangeOnly_PurePhysicsPath =====
// Classify value-range only -> Route (no data engine) -> PurePhysics -> Generate -> Validate -> PostFilter -> EvidenceV2

TEST(ConstraintPipeline, ValueRangeOnly_PurePhysicsPath) {
    Schema s = make_sensor_schema();

    // Step 1: Classify — value-range only
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range", "vib_range"};
    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok()) ;
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kRowByRow);
    EXPECT_EQ(cls_result.value().value_range_count, 2);
    EXPECT_EQ(cls_result.value().inter_row_count, 0);
    EXPECT_EQ(cls_result.value().aggregate_count, 0);

    // Step 2: Route — no data engine -> pure physics
    ExecutionRouter router(false);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok()) ;
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_EQ(routing.value().identity.identity, "physics_sampler");

    // Step 3: Generate (simulated — data within value range)
    auto table = make_data_table({0, 100, 200, 300, 400},
                                  {25.0, 30.0, 35.0, 40.0, 45.0});
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 5);

    // Step 4: Validate via PostFilter
    PostFilter pf;
    auto pf_result = pf.execute(table, 5);
    ASSERT_TRUE(pf_result.ok()) ;
    EXPECT_EQ(pf_result.value().pre_filter_rows, 5);

    // Step 5: Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok()) ;
    EXPECT_EQ(ep_result.value().schema_version, "v2");
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.value_range_count, 2);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 0);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.aggregate_count, 0);

    // Step 6: Audit trail
    AuditLog audit;
    audit.create_genesis();
    audit.append("generate", "physics_sampler",
                  {{"rows", "5"}, {"path", "pure_physics"}});
    EXPECT_EQ(audit.record_count(), 2);
    auto verify = audit.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}

// ===== Test 2: ValueRangeAndInterRow_PostFilterPath =====
// Classify (VR + inter-row) -> Route (data engine) -> PostFilter -> InterRow execute -> PostFilter -> EvidenceV2

TEST(ConstraintPipeline, ValueRangeAndInterRow_PostFilterPath) {
    Schema s = make_sensor_schema();

    // Step 1: Classify — value-range + inter-row
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
    cs.inter_row_defs.push_back(ird);

    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok()) ;
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kStatefulBatch);
    EXPECT_EQ(cls_result.value().value_range_count, 1);
    EXPECT_EQ(cls_result.value().inter_row_count, 1);

    // Step 2: Route — with data engine -> post-filter
    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok()) ;
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kPostFilter);

    // Step 3: Generate (simulated — data with gradual temperature changes)
    auto table = make_data_table({0, 100, 200, 300, 400},
                                  {10.0, 12.0, 14.0, 16.0, 18.0});

    // Step 4: Execute inter-row constraints
    InterRowEngine ir_engine(s, {ird});
    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok()) ;
    EXPECT_EQ(ir_result.value().rows_passed, 5);
    EXPECT_EQ(ir_result.value().rows_filtered, 0);

    // Step 5: Post-filter the inter-row output
    PostFilter pf;
    auto pf_result = pf.execute(ir_result.value().filtered_batch, 5);
    ASSERT_TRUE(pf_result.ok()) ;

    // Step 6: Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok()) ;
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.value_range_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.aggregate_count, 0);
}

// ===== Test 3: AllConstraintTypes_FullFunctionPath =====
// Classify (VR + IR + Agg) -> Route (data engine) -> FullFunction -> Aggregate -> PostFilter -> EvidenceV2

TEST(ConstraintPipeline, AllConstraintTypes_FullFunctionPath) {
    Schema s = make_sensor_schema();

    // Step 1: Classify — all three constraint types
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
    ASSERT_TRUE(cls_result.ok()) ;
    EXPECT_EQ(cls_result.value().execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_EQ(cls_result.value().value_range_count, 1);
    EXPECT_EQ(cls_result.value().inter_row_count, 1);
    EXPECT_EQ(cls_result.value().aggregate_count, 1);

    // Step 2: Route — with data engine -> full function
    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok()) ;
    EXPECT_EQ(routing.value().selected_path, DegradationPath::kFullFunction);

    // Step 3: Execute aggregate engine (two-phase)
    AggregateEngine agg_engine(s, {acd});
    auto table = make_data_table(
        {0, 100, 200, kOneHourUs, kOneHourUs + 100},
        {20.0, 25.0, 30.0, 22.0, 28.0});
    auto agg_result = agg_engine.execute(table, {});
    ASSERT_TRUE(agg_result.ok()) ;

    // Step 4: Post-filter the phase one output
    PostFilter pf;
    auto pf_result = pf.execute(agg_result.value().phase_one_output, 5);
    ASSERT_TRUE(pf_result.ok()) ;

    // Step 5: Build EvidencePackage v2
    EvidencePackageV2Builder ep_builder;
    auto ep_result = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep_result.ok()) ;
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.value_range_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.inter_row_count, 1);
    EXPECT_EQ(ep_result.value().constraint_type_breakdown.aggregate_count, 1);
}

// ===== Test 4: InterRowStrictFiltering =====
// Very tight delta constraint -> verify rows are filtered

TEST(ConstraintPipeline, InterRowStrictFiltering) {
    Schema s = make_sensor_schema();

    // Tight delta: temperature must not change by more than 2.0 between rows
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 2.0;

    InterRowEngine ir_engine(s, {ird});

    // Data where some rows violate the 2.0 delta:
    // 10.0 -> 11.0 (delta 1.0, OK)
    // 11.0 -> 14.0 (delta 3.0, VIOLATES)
    // 14.0 -> 15.0 (delta 1.0, OK)
    // 15.0 -> 20.0 (delta 5.0, VIOLATES)
    // 20.0 -> 21.0 (delta 1.0, OK)
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500},
        {10.0, 11.0, 14.0, 15.0, 20.0, 21.0});

    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok()) ;

    // Some rows should be filtered due to tight delta constraint
    EXPECT_GT(ir_result.value().rows_filtered, 0)
        << "Expected some rows to be filtered with tight delta_max=2.0";
    EXPECT_LT(ir_result.value().rows_passed, 6)
        << "Expected fewer than 6 rows passing with tight delta";
    EXPECT_GT(ir_result.value().filter_rate, 0.0)
        << "Expected non-zero filter rate";

    // Verify filtered batch is valid
    ASSERT_NE(ir_result.value().filtered_batch, nullptr);
    EXPECT_EQ(ir_result.value().filtered_batch->num_rows(),
              ir_result.value().rows_passed);

    // Now pipeline the filtered result through post-filter and evidence
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    cs.inter_row_defs.push_back(ird);

    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok());

    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());

    PostFilter pf;
    auto pf_result = pf.execute(ir_result.value().filtered_batch,
                                 ir_result.value().rows_passed);
    ASSERT_TRUE(pf_result.ok());

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        ir_result.value().rows_passed,
        ir_result.value().filter_rate,
        "post_filtered",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ep.value().constraint_type_breakdown.inter_row_count, 1);
}

// ===== Test 5: AggregateWindowComputation =====
// Test window computation with known data and verify aggregate values

TEST(ConstraintPipeline, AggregateWindowComputation) {
    Schema s = make_sensor_schema();

    // Create aggregate constraint: AVG(temperature) per 1-hour window <= 50.0
    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp_1h";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 50.0;
    acd.window_interval_us = kOneHourUs;

    AggregateEngine agg_engine(s, {acd});

    // Data across two windows:
    // Window 1 (0 - 1h): timestamps 0, 100, 200 with temps 20, 30, 40 -> avg = 30.0
    // Window 2 (1h - 2h): timestamps 1h, 1h+100 with temps 25, 35 -> avg = 30.0
    auto table = make_data_table(
        {0, 100, 200, kOneHourUs, kOneHourUs + 100},
        {20.0, 30.0, 40.0, 25.0, 35.0});

    // Compute windows
    auto windows_result = agg_engine.compute_windows(table, kOneHourUs);
    ASSERT_TRUE(windows_result.ok()) ;
    auto& windows = windows_result.value();
    ASSERT_GE(windows.size(), 1u);

    // Execute full two-phase
    auto agg_result = agg_engine.execute(table, {});
    ASSERT_TRUE(agg_result.ok()) ;
    ASSERT_NE(agg_result.value().phase_one_output, nullptr);

    // Verify windows were computed in phase two
    EXPECT_GT(agg_result.value().phase_two.total_windows, 0);

    // Now pipeline through post-filter and evidence
    PostFilter pf;
    auto pf_result = pf.execute(agg_result.value().phase_one_output, 5);
    ASSERT_TRUE(pf_result.ok());

    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    cs.aggregate_defs.push_back(acd);

    auto cls_result = classifier.classify(cs, s);
    ASSERT_TRUE(cls_result.ok());

    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ep.value().constraint_type_breakdown.aggregate_count, 1);

    // Verify JSON round-trip
    auto json_result = ep_builder.to_json(ep.value());
    ASSERT_TRUE(json_result.ok());
    auto parsed = ep_builder.from_json(json_result.value());
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().schema_version, "v2");
}

// ===== Test 6: AuditTrailThroughPipeline =====
// Full pipeline with audit log at each step -> verify chain integrity

TEST(ConstraintPipeline, AuditTrailThroughPipeline) {
    AuditLog audit;
    audit.create_genesis();

    Schema s = make_sensor_schema();

    // Step 1: Classify
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 10.0;
    cs.inter_row_defs.push_back(ird);

    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 50.0;
    acd.window_interval_us = kOneHourUs;
    cs.aggregate_defs.push_back(acd);

    auto cls = classifier.classify(cs, s);
    ASSERT_TRUE(cls.ok()) ;
    audit.append("classify", "constraint_classifier",
                  {{"vr", "1"}, {"ir", "1"}, {"agg", "1"}});

    // Step 2: Route
    ExecutionRouter router(true);
    auto routing = router.route(cls.value(), s);
    ASSERT_TRUE(routing.ok()) ;
    audit.append("route", "execution_router",
                  {{"path", "full_function"}});

    // Step 3: Generate (simulated)
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700},
        {20.0, 22.0, 24.0, 26.0, 28.0, 30.0, 32.0, 34.0});
    audit.append("generate", "data_engine",
                  {{"rows", std::to_string(table->num_rows())}});

    // Step 4: Inter-row filtering
    InterRowEngine ir_engine(s, {ird});
    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok()) ;
    audit.append("inter_row", "inter_row_engine",
                  {{"passed", std::to_string(ir_result.value().rows_passed)},
                   {"filtered", std::to_string(ir_result.value().rows_filtered)}});

    // Step 5: Aggregate check
    AggregateEngine agg_engine(s, {acd});
    auto agg_result = agg_engine.execute(
        ir_result.value().filtered_batch, ir_result.value().outgoing_states);
    ASSERT_TRUE(agg_result.ok()) ;
    audit.append("aggregate", "aggregate_engine",
                  {{"windows", std::to_string(agg_result.value().phase_two.total_windows)}});

    // Step 6: Post-filter
    PostFilter pf;
    auto pf_result = pf.execute(agg_result.value().phase_one_output, 8);
    ASSERT_TRUE(pf_result.ok()) ;
    audit.append("post_filter", "post_filter_engine");

    // Step 7: Build evidence
    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        8, 0.0, "physics_guaranteed",
        routing.value(), cls.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok()) ;
    audit.append("evidence_build", "evidence_builder");

    // Verify full audit chain
    EXPECT_EQ(audit.record_count(), 8);  // genesis + 7 steps
    auto verify = audit.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());

    // Daily verification should pass
    auto report = audit.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 8);

    // Scan the audit log to verify all operations are present
    auto scan_result = audit.scan({}, {});
    ASSERT_TRUE(scan_result.ok());
    EXPECT_EQ(scan_result.value().size(), 8u);

    // Verify each step is recorded
    bool found_classify = false, found_route = false, found_generate = false;
    bool found_inter_row = false, found_aggregate = false;
    bool found_post_filter = false, found_evidence = false;
    for (const auto& rec : scan_result.value()) {
        if (rec.operation == "classify") found_classify = true;
        if (rec.operation == "route") found_route = true;
        if (rec.operation == "generate") found_generate = true;
        if (rec.operation == "inter_row") found_inter_row = true;
        if (rec.operation == "aggregate") found_aggregate = true;
        if (rec.operation == "post_filter") found_post_filter = true;
        if (rec.operation == "evidence_build") found_evidence = true;
    }
    EXPECT_TRUE(found_classify);
    EXPECT_TRUE(found_route);
    EXPECT_TRUE(found_generate);
    EXPECT_TRUE(found_inter_row);
    EXPECT_TRUE(found_aggregate);
    EXPECT_TRUE(found_post_filter);
    EXPECT_TRUE(found_evidence);
}
