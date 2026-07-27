// V2+V3 Chaos Integration Tests
// Aggressive tests that probe boundary conditions and integration gaps
// between v2 pipeline components and v3 storage/alignment components.
#include <gtest/gtest.h>

#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/alignment/drift_detector.h"
#include "engine/alignment/continuous_alignment_engine.h"
#include "storage/version/model_version_chain.h"
#include "storage/gc/gc_compactor.h"
#include "storage/gc/protection.h"
#include "storage/timetravel/time_travel_engine.h"
#include "storage/model/model_storage_layer.h"
#include "storage/metadata.h"
#include "schema/schema.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::engine::router;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::alignment;
using namespace synthgen::storage;
using namespace synthgen::storage::version;
using namespace synthgen::storage::gc;
using namespace synthgen::storage::model;
using namespace synthgen::storage::timetravel;
using namespace synthgen::schema;

namespace {

constexpr int64_t kOneHourUs = 3600000000LL;

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

std::vector<double> generate_normal(
    double mean, double stddev, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

// Write checkpoint directly to disk, bypassing ModelStorageLayer LRU cache.
void write_checkpoint_file(
    const std::string& storage_root,
    const std::string& model_name,
    const std::string& version_id,
    const std::string& data) {
    auto dir = std::filesystem::path(storage_root) / "models" / model_name;
    std::filesystem::create_directories(dir);
    auto path = dir / (version_id + ".parquet");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
}

}  // namespace

// ============================================================
// v2 Pipeline Chaos Tests
// ============================================================

// ---- Test 1: InterRow with MonotoneIncrease ----
// Existing tests only cover kDeltaMax. MonotoneIncrease requires x[t] > x[t-1]
// strictly. Feed monotonically increasing data -> all pass.
// Then feed non-monotone data -> some rows should be filtered.
TEST(V2V3Chaos, InterRow_MonotoneIncrease) {
    Schema s = make_sensor_schema();

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kMonotoneIncrease;

    InterRowEngine engine(s, {ird});

    // Strictly increasing data: 10, 20, 30, 40, 50
    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {10.0, 20.0, 30.0, 40.0, 50.0});
    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    // First row always passes (no predecessor), then each subsequent row passes
    EXPECT_EQ(result.value().rows_passed, 5);
    EXPECT_EQ(result.value().rows_filtered, 0);

    // Non-monotone data: 10, 5, 30, 25, 50 -> rows at indices 1,3 should be filtered
    auto table2 = make_data_table(
        {0, 100, 200, 300, 400},
        {10.0, 5.0, 30.0, 25.0, 50.0});
    auto result2 = engine.execute_batch(table2, {});
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_GT(result2.value().rows_filtered, 0)
        << "MonotoneIncrease should filter rows where x[t] <= x[t-1]";
    EXPECT_LT(result2.value().rows_passed, 5);
}

// ---- Test 2: InterRow with MonotoneDecrease ----
// Strictly decreasing constraint: x[t] < x[t-1].
TEST(V2V3Chaos, InterRow_MonotoneDecrease) {
    Schema s = make_sensor_schema();

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kMonotoneDecrease;

    InterRowEngine engine(s, {ird});

    // Strictly decreasing data: 50, 40, 30, 20, 10
    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {50.0, 40.0, 30.0, 20.0, 10.0});
    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_passed, 5);
    EXPECT_EQ(result.value().rows_filtered, 0);

    // Non-decreasing data: 50, 55, 30, 35, 10 -> rows 1,3 should be filtered
    auto table2 = make_data_table(
        {0, 100, 200, 300, 400},
        {50.0, 55.0, 30.0, 35.0, 10.0});
    auto result2 = engine.execute_batch(table2, {});
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_GT(result2.value().rows_filtered, 0)
        << "MonotoneDecrease should filter rows where x[t] >= x[t-1]";
    EXPECT_LT(result2.value().rows_passed, 5);
}

// ---- Test 3: Aggregate with kSum function ----
// Existing tests only cover kAvg. Test kSum with a window constraint.
TEST(V2V3Chaos, Aggregate_SumFunction) {
    Schema s = make_sensor_schema();

    AggregateConstraintDef acd;
    acd.constraint_name = "sum_vib";
    acd.column_name = "vibration";
    acd.function = AggregateFunction::kSum;
    acd.max_val = 20.0;
    acd.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {acd});

    // 10 rows within 1 hour, vibration values sum to 15.0 < 20.0 -> pass
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0},
        {1.0, 1.5, 2.0, 1.0, 1.5, 2.0, 1.0, 1.5, 2.0, 1.5});
    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Phase two should produce at least one window
    EXPECT_GT(result.value().phase_two.total_windows, 0)
        << "Should have at least one aggregation window";

    // Now test with vibration values that sum > 20.0 -> should have violations
    auto table2 = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0, 25.0},
        {3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0});  // sum=30 > 20
    auto result2 = engine.execute(table2, {});
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_GT(result2.value().phase_two.windows_violated, 0)
        << "Sum=30 exceeds max=20, should have violations";
}

// ---- Test 4: Aggregate with kMin function ----
TEST(V2V3Chaos, Aggregate_MinFunction) {
    Schema s = make_sensor_schema();

    AggregateConstraintDef acd;
    acd.constraint_name = "min_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kMin;
    acd.min_val = -10.0;  // minimum temperature in any window must be >= -10
    acd.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {acd});

    // All temps >= -10.0 -> no violations
    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {5.0, 10.0, 15.0, 20.0, 25.0});
    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Data with min -5.0 >= -10.0, no violations
    EXPECT_EQ(result.value().phase_two.windows_violated, 0)
        << "Min=-5.0 >= -10.0, should have no violations";

    // Data with min -20.0 < -10.0 -> violation
    auto table2 = make_data_table(
        {0, 100, 200, 300, 400},
        {-20.0, 10.0, 15.0, 20.0, 25.0});
    auto result2 = engine.execute(table2, {});
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_GT(result2.value().phase_two.windows_violated, 0)
        << "Min=-20.0 < -10.0, should have violations";
}

// ---- Test 5: Aggregate with kMax function ----
TEST(V2V3Chaos, Aggregate_MaxFunction) {
    Schema s = make_sensor_schema();

    AggregateConstraintDef acd;
    acd.constraint_name = "max_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kMax;
    acd.max_val = 50.0;  // maximum temperature in any window must be <= 50
    acd.window_interval_us = kOneHourUs;

    AggregateEngine engine(s, {acd});

    // All temps <= 50.0 -> no violations
    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {5.0, 10.0, 15.0, 20.0, 25.0});
    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().phase_two.windows_violated, 0)
        << "Max=25.0 <= 50.0, should have no violations";

    // Data with max 60.0 > 50.0 -> violation
    auto table2 = make_data_table(
        {0, 100, 200, 300, 400},
        {5.0, 10.0, 60.0, 20.0, 25.0});
    auto result2 = engine.execute(table2, {});
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_GT(result2.value().phase_two.windows_violated, 0)
        << "Max=60.0 > 50.0, should have violations";
}

// ---- Test 6: PostFilter with aggressive custom config ----
// Use high_exclusion_threshold=0.5 (tighter than default 0.80) and
// oversampling_ratio=5.0 (higher than default 3.0).
TEST(V2V3Chaos, PostFilter_CustomConfig) {
    PostFilterConfig config;
    config.high_exclusion_threshold = 0.5;
    config.critical_exclusion_threshold = 0.7;
    config.oversampling_ratio = 5.0;
    config.enable_realtime_monitoring = true;

    PostFilter pf(config);

    // Verify config took effect
    EXPECT_DOUBLE_EQ(pf.config().high_exclusion_threshold, 0.5);
    EXPECT_DOUBLE_EQ(pf.config().critical_exclusion_threshold, 0.7);
    EXPECT_DOUBLE_EQ(pf.config().oversampling_ratio, 5.0);

    // Create data where some rows have out-of-range values
    auto table = make_data_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {25.0, 30.0, 35.0, 100.0, 40.0, 45.0, 200.0, 50.0, 55.0, 60.0});
    auto result = pf.execute(table, 10);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // With the custom threshold, classify the exclusion rate
    EXPECT_GE(result.value().pre_filter_rows, result.value().post_filter_rows);

    // The custom threshold should classify exclusion rates differently
    // With 0.5 threshold, even moderate exclusion triggers "high" band
    double rate = result.value().actual_exclusion_rate;
    if (rate > 0.0) {
        auto band = PostFilter::classify_exclusion_rate(rate, 0.7);
        // With the lowered thresholds, the band classification may shift
        EXPECT_NE(band, ExclusionRateBand::kLow);
    }
}

// ---- Test 7: EvidenceV2 JSON round-trip with full field verification ----
// Build evidence, serialize to JSON, deserialize, verify ALL fields match.
TEST(V2V3Chaos, EvidenceV2_JsonRoundTrip_AllFields) {
    Schema s = make_sensor_schema();

    // Set up classification with all three constraint types
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range", "vib_range"};
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;
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

    ExecutionRouter router(true);
    auto routing = router.route(cls_result.value(), s);
    ASSERT_TRUE(routing.ok());

    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {10.0, 12.0, 14.0, 16.0, 18.0});
    PostFilter pf;
    auto pf_result = pf.execute(table, 5);
    ASSERT_TRUE(pf_result.ok());

    EvidencePackageV2Builder builder;
    auto ep = builder.build(
        5, 0.05, "physics_guaranteed",
        routing.value(), cls_result.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok()) << ep.error().message;

    // Serialize to JSON
    auto json_result = builder.to_json(ep.value());
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;
    EXPECT_FALSE(json_result.value().empty());

    // Deserialize from JSON
    auto parsed = builder.from_json(json_result.value());
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;

    // Verify ALL fields match
    const auto& orig = ep.value();
    const auto& rt = parsed.value();

    // v1-inherited fields
    EXPECT_EQ(rt.schema_version, orig.schema_version);
    EXPECT_EQ(rt.schema_hash, orig.schema_hash);
    EXPECT_DOUBLE_EQ(rt.exclusion_rate, orig.exclusion_rate);
    EXPECT_EQ(rt.data_grade, orig.data_grade);
    EXPECT_EQ(rt.row_count, orig.row_count);

    // Tail report fields
    EXPECT_EQ(rt.epistemological_bias, orig.epistemological_bias);
    EXPECT_EQ(rt.tail_exclusion_statement, orig.tail_exclusion_statement);
    EXPECT_DOUBLE_EQ(rt.exclusion_rate_report, orig.exclusion_rate_report);
    EXPECT_EQ(rt.rows_generated, orig.rows_generated);
    EXPECT_EQ(rt.rows_validated, orig.rows_validated);
    EXPECT_EQ(rt.rows_failed_validation, orig.rows_failed_validation);
    EXPECT_EQ(rt.distribution_used, orig.distribution_used);
    EXPECT_EQ(rt.seed_used, orig.seed_used);

    // v2 new fields
    EXPECT_EQ(rt.audit_immutability, orig.audit_immutability);
    EXPECT_EQ(rt.statistical_fidelity.available, orig.statistical_fidelity.available);
    EXPECT_EQ(rt.statistical_fidelity.model_version, orig.statistical_fidelity.model_version);
    EXPECT_DOUBLE_EQ(rt.statistical_fidelity.fidelity_score, orig.statistical_fidelity.fidelity_score);
    EXPECT_EQ(rt.statistical_fidelity.training_rows, orig.statistical_fidelity.training_rows);

    // Constraint type breakdown
    EXPECT_EQ(rt.constraint_type_breakdown.value_range_count,
              orig.constraint_type_breakdown.value_range_count);
    EXPECT_EQ(rt.constraint_type_breakdown.inter_row_count,
              orig.constraint_type_breakdown.inter_row_count);
    EXPECT_EQ(rt.constraint_type_breakdown.aggregate_count,
              orig.constraint_type_breakdown.aggregate_count);

    // Generator identity
    EXPECT_EQ(rt.generator_identity.identity, orig.generator_identity.identity);
    EXPECT_EQ(rt.generator_identity.justification, orig.generator_identity.justification);
    EXPECT_EQ(rt.generator_identity.path, orig.generator_identity.path);

    // Post-filter info
    EXPECT_EQ(rt.post_filter_info.was_post_filtered, orig.post_filter_info.was_post_filtered);
    EXPECT_EQ(rt.post_filter_info.pre_filter_rows, orig.post_filter_info.pre_filter_rows);
    EXPECT_EQ(rt.post_filter_info.post_filter_rows, orig.post_filter_info.post_filter_rows);
    EXPECT_DOUBLE_EQ(rt.post_filter_info.actual_exclusion_rate,
                      orig.post_filter_info.actual_exclusion_rate);
    EXPECT_EQ(rt.post_filter_info.exclusion_rate_band,
              orig.post_filter_info.exclusion_rate_band);
    EXPECT_EQ(rt.post_filter_info.was_timeout_truncated,
              orig.post_filter_info.was_timeout_truncated);
}

// ============================================================
// v3 Pipeline Chaos Tests
// ============================================================

// Fixture for v3 tests requiring storage infrastructure
class V3ChaosTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_v3_chaos_" + std::to_string(::getpid()))).string();
    std::string storage_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_v3_chaos_" + std::to_string(::getpid())) / "storage").string();
    MetadataManager meta{test_dir};
    ModelVersionChain chain{meta};
    ModelStorageLayer storage{storage_dir};

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        std::filesystem::create_directories(storage_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    std::vector<ModelVersion> create_versions_with_checkpoints(
        const std::string& model, int count) {
        std::vector<ModelVersion> result;
        std::string parent;
        for (int i = 0; i < count; ++i) {
            ModelVersion v;
            v.model_name = model;
            v.fidelity_score = 0.9 - i * 0.01;
            v.training_rows = 1000 + i * 100;
            auto r = chain.create_version(model, parent, v);
            if (r.ok()) {
                write_checkpoint_file(
                    storage_dir, model, r.value().version_id,
                    "model_data_v" + std::to_string(i));
                result.push_back(r.value());
                parent = r.value().version_id;
            }
        }
        return result;
    }
};

// ---- Test 8: DriftDetector with identical distributions ----
// Same data -> no drift. p_value should be high (> significance level).
TEST_F(V3ChaosTest, DriftDetector_IdenticalDistributions) {
    DriftDetector detector("ks", 0.05);

    auto data = generate_normal(5.0, 1.0, 200, 42);

    // Compare distribution with itself
    auto result = detector.detect(data, data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // No drift should be detected for identical distributions
    EXPECT_FALSE(result.value().drift_detected)
        << "Identical distributions should not trigger drift detection";
    EXPECT_GT(result.value().p_value, 0.05)
        << "p_value should be high for identical distributions, got: "
        << result.value().p_value;
    EXPECT_DOUBLE_EQ(result.value().ks_statistic, 0.0)
        << "KS statistic should be 0.0 for identical distributions";

    // Also test with two independently generated distributions from
    // the same population (different seeds but same params)
    auto data2 = generate_normal(5.0, 1.0, 200, 99);
    auto result2 = detector.detect(data, data2);
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    // Same population should usually not detect drift (might occasionally,
    // but the KS statistic should be small)
    EXPECT_LT(result2.value().ks_statistic, 0.15)
        << "Same-population KS statistic should be small, got: "
        << result2.value().ks_statistic;
}

// ---- Test 9: DriftDetector with very different distributions ----
// Mean shift of 50 -> should detect drift with high confidence.
TEST_F(V3ChaosTest, DriftDetector_LargeMeanShift) {
    DriftDetector detector("ks", 0.05);

    auto baseline = generate_normal(0.0, 1.0, 200, 42);
    auto shifted  = generate_normal(50.0, 1.0, 200, 123);

    auto result = detector.detect(baseline, shifted);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Drift should definitely be detected
    EXPECT_TRUE(result.value().drift_detected)
        << "Mean shift of 50 should trigger drift detection";
    EXPECT_LT(result.value().p_value, 0.001)
        << "p_value should be extremely low for mean shift=50, got: "
        << result.value().p_value;
    EXPECT_GT(result.value().ks_statistic, 0.9)
        << "KS statistic should be near 1.0 for non-overlapping distributions, got: "
        << result.value().ks_statistic;
    EXPECT_GT(result.value().drift_score, 0.5)
        << "Drift score should be high for large mean shift";
}

// ---- Test 10: ContinuousAlignmentEngine: 10 rapid updates ----
// Push 10 updates through alignment engine. Verify:
// (a) all 10 versions created, (b) version chain grows correctly,
// (c) no duplicate version IDs, (d) parent chain is consistent.
TEST_F(V3ChaosTest, ContinuousAlignment_RapidUpdates) {
    ContinuousAlignmentEngine engine(chain, storage);

    std::vector<std::string> version_ids;
    std::string prev_id;

    for (int i = 0; i < 10; ++i) {
        AlignmentRequest req;
        req.model_name = "rapid_model";
        req.current_version_id = prev_id;
        // Vary the mean slightly to trigger drift on some iterations
        req.current_data = generate_normal(0.0, 1.0, 100, 42);
        req.new_data = generate_normal(static_cast<double>(i) * 2.0, 1.0, 100, 100 + i);

        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << "update_model failed at iteration " << i
                                  << ": " << result.error().message;

        std::string new_vid = result.value().new_version.version_id;
        EXPECT_FALSE(new_vid.empty())
            << "Version ID should not be empty at iteration " << i;

        // Verify no duplicate version IDs
        for (const auto& prev_vid : version_ids) {
            EXPECT_NE(new_vid, prev_vid)
                << "Duplicate version ID at iteration " << i;
        }

        // Verify parent chain consistency
        if (!prev_id.empty()) {
            EXPECT_EQ(result.value().new_version.parent_version_id, prev_id)
                << "Parent mismatch at iteration " << i;
        }

        version_ids.push_back(new_vid);
        prev_id = new_vid;
    }

    // Verify version chain has exactly 10 versions
    auto list = chain.list_versions("rapid_model");
    ASSERT_TRUE(list.ok()) << list.error().message;
    EXPECT_EQ(list.value().size(), 10u)
        << "Expected 10 versions in chain, got " << list.value().size();
}

// ---- Test 11: GcCompactor with keep_recent_n=1 ----
// Only 1 recent version is protected. Everything else is compactable.
// Create 10 versions, compact -> should compact 9 (keeping only the latest).
TEST_F(V3ChaosTest, GcCompactor_KeepOnlyOne) {
    ProtectionConfig config;
    config.keep_recent_n = 1;
    ProtectionChecker checker(config);
    GcCompactor compactor(chain, checker, config);

    auto versions = create_versions_with_checkpoints("sparse_model", 10);
    ASSERT_EQ(versions.size(), 10u);

    auto result = compactor.compact("sparse_model");
    ASSERT_TRUE(result.ok()) << result.error().message;

    // With keep_recent_n=1, only the most recent version is protected.
    // The other 9 should be compacted.
    EXPECT_EQ(result.value().compacted_versions.size(), 9u)
        << "Expected 9 compacted versions (10 total - 1 recent), got: "
        << result.value().compacted_versions.size();
    EXPECT_FALSE(result.value().merged_version_id.empty());

    // The most recent version should NOT be in the compacted list
    const auto& latest = versions.back();
    auto it = std::find(result.value().compacted_versions.begin(),
                        result.value().compacted_versions.end(),
                        latest.version_id);
    EXPECT_EQ(it, result.value().compacted_versions.end())
        << "Latest version should not be compacted";

    // Verify the latest version is still accessible
    auto get_result = chain.get_version(latest.version_id);
    EXPECT_TRUE(get_result.ok());
    EXPECT_EQ(get_result.value()->version_id, latest.version_id);
}

// ---- Test 12: TimeTravelEngine: query non-existent version_id ----
// Should degrade gracefully or return an error (not crash).
TEST_F(V3ChaosTest, TimeTravel_NonExistentVersion) {
    // Create a few versions first
    auto versions = create_versions_with_checkpoints("ghost_model", 3);
    ASSERT_EQ(versions.size(), 3u);

    TimeTravelEngine tt_engine(chain, storage);

    // Query a version that does not exist
    auto result = tt_engine.query_as_of("ghost_model", "nonexistent_version_xyz");

    // The engine should either return an error (not crash).
    // Acceptable outcomes: error result OR degraded result with was_degraded=true.
    if (result.ok()) {
        // If it succeeds, it must degrade gracefully
        EXPECT_TRUE(result.value().was_degraded)
            << "Querying non-existent version should degrade";
    }
    // If it returns an error, that is also acceptable behavior
    // Key assertion: we got here without crashing
}

// ---- Test 12b: TimeTravelEngine: query non-existent model ----
// Query a model that does not exist at all.
TEST_F(V3ChaosTest, TimeTravel_NonExistentModel) {
    TimeTravelEngine tt_engine(chain, storage);

    auto result = tt_engine.query_as_of("phantom_model", "some_version");
    // Should not crash. Either error or degraded.
    if (result.ok()) {
        EXPECT_TRUE(result.value().was_degraded);
    }
    // Key: no crash
}

// ---- Bonus Test: Cross v2-v3 boundary ----
// Full pipeline: classify -> route -> generate -> post-filter -> evidence v2
// Then store the evidence as a model version and time-travel back to it.
TEST_F(V3ChaosTest, V2Pipeline_To_V3Storage) {
    Schema s = make_sensor_schema();

    // v2 pipeline
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kMonotoneIncrease;
    cs.inter_row_defs.push_back(ird);

    auto cls = classifier.classify(cs, s);
    ASSERT_TRUE(cls.ok()) << cls.error().message;

    ExecutionRouter router(true);
    auto routing = router.route(cls.value(), s);
    ASSERT_TRUE(routing.ok()) << routing.error().message;

    // Monotonically increasing data
    auto table = make_data_table(
        {0, 100, 200, 300, 400},
        {10.0, 20.0, 30.0, 40.0, 50.0});

    PostFilter pf;
    auto pf_result = pf.execute(table, 5);
    ASSERT_TRUE(pf_result.ok()) << pf_result.error().message;

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        5, 0.0, "physics_guaranteed",
        routing.value(), cls.value(), pf_result.value(), s);
    ASSERT_TRUE(ep.ok()) << ep.error().message;

    // Now bridge into v3: store evidence as a model version
    auto json = ep_builder.to_json(ep.value());
    ASSERT_TRUE(json.ok()) << json.error().message;

    ModelVersion v;
    v.model_name = "evidence_bridge_model";
    v.fidelity_score = 1.0 - ep.value().exclusion_rate;
    v.training_rows = ep.value().row_count;
    v.custom_metadata["data_grade"] = ep.value().data_grade;

    auto create_result = chain.create_version(
        "evidence_bridge_model", "", v);
    ASSERT_TRUE(create_result.ok()) << create_result.error().message;

    write_checkpoint_file(
        storage_dir, "evidence_bridge_model",
        create_result.value().version_id, json.value());

    // Time travel back to this version and verify
    TimeTravelEngine tt(chain, storage);
    auto tt_result = tt.query_as_of(
        "evidence_bridge_model",
        create_result.value().version_id);
    ASSERT_TRUE(tt_result.ok()) << tt_result.error().message;
    EXPECT_FALSE(tt_result.value().was_degraded);
    EXPECT_EQ(tt_result.value().data, json.value());

    // Parse back and verify
    auto reparsed = ep_builder.from_json(tt_result.value().data);
    ASSERT_TRUE(reparsed.ok()) << reparsed.error().message;
    EXPECT_EQ(reparsed.value().schema_version, "v2");
    EXPECT_EQ(reparsed.value().row_count, 5);
    EXPECT_EQ(reparsed.value().data_grade, "physics_guaranteed");
}
