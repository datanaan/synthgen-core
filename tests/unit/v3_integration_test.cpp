#include <gtest/gtest.h>

#include "storage/version/model_version_chain.h"
#include "storage/gc/gc_compactor.h"
#include "storage/gc/protection.h"
#include "storage/timetravel/time_travel_engine.h"
#include "storage/model/model_storage_layer.h"
#include "storage/metadata.h"
#include "engine/alignment/drift_detector.h"
#include "engine/alignment/continuous_alignment_engine.h"
#include "engine/evidence/tail_report_v3.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::storage;
using namespace synthgen::storage::version;
using namespace synthgen::storage::gc;
using namespace synthgen::storage::model;
using namespace synthgen::storage::timetravel;
using namespace synthgen::engine::alignment;
using namespace synthgen::engine::evidence;

namespace {

std::vector<double> generate_normal(
    double mean, double stddev, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

// Write checkpoint directly to disk (bypasses ModelStorageLayer cache so
// that file deletion is correctly reflected on subsequent load_model calls).
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

class V3IntegrationTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_v3_integ_" + std::to_string(::getpid()))).string();
    std::string storage_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_v3_integ_" + std::to_string(::getpid())) / "storage").string();
    MetadataManager meta{test_dir};
    ModelVersionChain chain{meta};
    ModelStorageLayer storage{storage_dir};
    ProtectionConfig gc_config;
    ProtectionChecker checker{gc_config};
    GcCompactor compactor{chain, checker, gc_config};
    TimeTravelEngine time_travel{chain, storage};
    ContinuousAlignmentEngine alignment_engine{chain, storage};

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        std::filesystem::create_directories(storage_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    // Helper: create N versions with checkpoints written directly to disk.
    // Direct disk writes ensure that file deletion (simulating compaction)
    // is visible to subsequent load_model calls.
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

// ---------------------------------------------------------------------------
// Test 1: Full version chain flow
// Create 5 versions, list them, verify count and sort order (descending).
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, FullVersionChainFlow) {
    auto versions = create_versions_with_checkpoints("sensor_model", 5);
    ASSERT_EQ(versions.size(), 5u);

    // List versions — should return all 5
    auto list_result = chain.list_versions("sensor_model");
    ASSERT_TRUE(list_result.ok()) << list_result.error().message;
    EXPECT_EQ(list_result.value().size(), 5u);

    // Each version should be individually retrievable
    for (const auto& v : versions) {
        auto get_result = chain.get_version(v.version_id);
        ASSERT_TRUE(get_result.ok()) << get_result.error().message;
        EXPECT_EQ(get_result.value()->version_id, v.version_id);
        EXPECT_EQ(get_result.value()->model_name, "sensor_model");
    }

    // Verify parent chain: each version's parent matches the previous
    for (size_t i = 1; i < versions.size(); ++i) {
        EXPECT_EQ(versions[i].parent_version_id,
                  versions[i - 1].version_id);
    }

    // First version should have empty parent
    EXPECT_TRUE(versions[0].is_first_version());
    EXPECT_TRUE(versions[0].parent_version_id.empty());
}

// ---------------------------------------------------------------------------
// Test 2: Compaction and time travel with degradation
// Create 15 versions, compact (keep_recent_n=10, compacts 5 oldest),
// simulate compaction by deleting checkpoint files for compacted versions,
// then time-travel to a compacted version -> should degrade with bias report.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, CompactionAndTimeTravel) {
    auto versions = create_versions_with_checkpoints("weather_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // Run compaction — with keep_recent_n=10, the 5 oldest are compactable
    auto compact_result = compactor.compact("weather_model");
    ASSERT_TRUE(compact_result.ok()) << compact_result.error().message;
    const auto& cr = compact_result.value();
    EXPECT_EQ(cr.compacted_versions.size(), 5u);
    EXPECT_FALSE(cr.merged_version_id.empty());

    // Simulate compaction by deleting checkpoint files for compacted versions
    for (const auto& vid : cr.compacted_versions) {
        auto path = std::filesystem::path(storage_dir) / "models" /
                    "weather_model" / (vid + ".parquet");
        ASSERT_TRUE(std::filesystem::exists(path));
        std::filesystem::remove(path);
    }

    // Time-travel to the first compacted version -> should degrade
    auto& target = versions[0];
    auto tt_result = time_travel.query_as_of("weather_model",
                                             target.version_id);
    ASSERT_TRUE(tt_result.ok()) << tt_result.error().message;
    EXPECT_TRUE(tt_result.value().was_degraded);

    // Should have a bias report
    ASSERT_TRUE(tt_result.value().bias_report.has_value());
    const auto& report = tt_result.value().bias_report.value();
    EXPECT_EQ(report.requested_version, target.version_id);
    EXPECT_TRUE(report.version_mismatch);

    // Returned data should come from a non-compacted version
    EXPECT_FALSE(tt_result.value().data.empty());
}

// ---------------------------------------------------------------------------
// Test 3: Alignment drift detection
// Create initial version, run alignment with significantly drifted data,
// verify drift_detected=true and a new version is created.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, AlignmentDriftDetection) {
    // First alignment: establish baseline
    AlignmentRequest req1;
    req1.model_name = "sensor_model";
    req1.current_version_id = "";
    req1.current_data = {};
    req1.new_data = generate_normal(0.0, 1.0, 200, 42);
    auto r1 = alignment_engine.update_model(req1);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    EXPECT_FALSE(r1.value().new_version.version_id.empty());
    EXPECT_FALSE(r1.value().drift_detected);  // First alignment, no drift

    auto first_version_id = r1.value().new_version.version_id;

    // Second alignment: significantly shifted data
    AlignmentRequest req2;
    req2.model_name = "sensor_model";
    req2.current_version_id = first_version_id;
    req2.current_data = generate_normal(0.0, 1.0, 200, 42);
    req2.new_data = generate_normal(10.0, 1.0, 200, 123);  // big shift
    auto r2 = alignment_engine.update_model(req2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;

    // Drift should be detected
    EXPECT_TRUE(r2.value().drift_detected);
    EXPECT_GT(r2.value().drift_score, 0.1);

    // New version should be created with correct parent
    EXPECT_FALSE(r2.value().new_version.version_id.empty());
    EXPECT_EQ(r2.value().new_version.parent_version_id, first_version_id);

    // Version chain should have 2 versions
    auto versions = chain.list_versions("sensor_model");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 2u);
}

// ---------------------------------------------------------------------------
// Test 4: TailReportV3 integration
// Create version, generate exclusion rates 0.1 / 0.5 / 0.8, verify rate
// bands and data grades map correctly.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, TailReportV3_Integration) {
    auto versions = create_versions_with_checkpoints("sensor_model", 1);
    ASSERT_FALSE(versions.empty());

    // Rate 0.1 -> low band -> statistics_guaranteed
    {
        TailReportV3 report = build_tail_report_v3(
            0.1, false, "converged", 1234567890LL);
        EXPECT_DOUBLE_EQ(report.exclusion_rate, 0.1);
        EXPECT_EQ(report.rate_band, "low");
        EXPECT_EQ(report.data_grade, "statistics_guaranteed");
        EXPECT_FALSE(report.fidelity_mismatch);
        EXPECT_TRUE(report.mismatch_reason.empty());
        EXPECT_EQ(report.compensation_status, "converged");
        EXPECT_TRUE(report.tail_exclusion_statement.find("10%") !=
                    std::string::npos);
    }

    // Rate 0.5 -> medium band -> limited_fidelity
    {
        TailReportV3 report = build_tail_report_v3(
            0.5, false, "converging", 1234567890LL);
        EXPECT_DOUBLE_EQ(report.exclusion_rate, 0.5);
        EXPECT_EQ(report.rate_band, "medium");
        EXPECT_EQ(report.data_grade, "limited_fidelity");
        EXPECT_EQ(report.compensation_status, "converging");
    }

    // Rate 0.8 -> high band + degradation -> limited_fidelity_conservative
    {
        TailReportV3 report = build_tail_report_v3(
            0.8, true, "diverging", 1234567890LL);
        EXPECT_DOUBLE_EQ(report.exclusion_rate, 0.8);
        EXPECT_EQ(report.rate_band, "high");
        EXPECT_EQ(report.data_grade, "limited_fidelity_conservative");
        EXPECT_TRUE(report.fidelity_mismatch);
        EXPECT_EQ(report.mismatch_reason, "compaction_degraded");
        EXPECT_EQ(report.compensation_status, "diverging");
        EXPECT_TRUE(report.tail_exclusion_statement.find("80%") !=
                    std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Test 5: Compaction protection for anchored versions
// Create 15 versions, anchor version[2] (v3), compact -> v3 NOT compacted.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, CompactionProtection_Anchored) {
    auto versions = create_versions_with_checkpoints("sensor_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // Anchor the 3rd version (index 2)
    checker.anchor(versions[2].version_id);

    auto compact_result = compactor.compact("sensor_model");
    ASSERT_TRUE(compact_result.ok()) << compact_result.error().message;
    const auto& cr = compact_result.value();

    // The anchored version should NOT appear in the compacted list
    auto it = std::find(cr.compacted_versions.begin(),
                        cr.compacted_versions.end(),
                        versions[2].version_id);
    EXPECT_EQ(it, cr.compacted_versions.end())
        << "Anchored version should not be compacted";

    // Verify the anchored version is still accessible in the chain
    auto get_result = chain.get_version(versions[2].version_id);
    EXPECT_TRUE(get_result.ok());
    EXPECT_EQ(get_result.value()->version_id, versions[2].version_id);
}

// ---------------------------------------------------------------------------
// Test 6: Alignment convergence
// Run 4 alignments with decreasing drift (same distribution).
// The compensation_status should transition: converging -> converged.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, AlignmentConvergence) {
    std::string prev_id;

    for (int i = 0; i < 4; ++i) {
        AlignmentRequest req;
        req.model_name = "converging_model";
        req.current_version_id = prev_id;
        // Use the same distribution for both current and new data
        req.current_data = generate_normal(5.0, 1.0, 200, 42);
        req.new_data = generate_normal(5.0, 1.0, 200, 42 + i * 10);

        auto result = alignment_engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
        prev_id = result.value().new_version.version_id;

        // After the first alignment (i=0) there is no drift comparison.
        // From i=1 onward, same-distribution data yields low drift.
        if (i == 0) {
            EXPECT_FALSE(result.value().drift_detected);
        }
        if (i >= 2) {
            // By the 3rd alignment (i=2), we have 3 drift scores all
            // below the convergence threshold -> converged
            EXPECT_EQ(result.value().compensation_status, "converged")
                << "Expected converged at iteration " << i;
        }
    }

    // Verify 4 versions in the chain
    auto versions = chain.list_versions("converging_model");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 4u);
}

// ---------------------------------------------------------------------------
// Test 7: Atomic write and recovery
// atomic_write a version, simulate interrupt (create .pending file),
// recover, verify the .pending file is cleaned up.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, AtomicWriteAndRecovery) {
    // Step 1: Create a version and atomic_write it
    ModelVersion v;
    v.version_id = "v_atomic_001";
    v.model_name = "test_model";
    v.fidelity_score = 0.95;

    auto write_result = storage.atomic_write("test_model", "atomic_blob_data", v);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    // Verify the data is loadable
    auto load = storage.load_model("test_model", "v_atomic_001");
    ASSERT_TRUE(load.ok()) << load.error().message;
    EXPECT_EQ(load.value(), "atomic_blob_data");

    // Verify no .pending file remains
    auto pending_path = std::filesystem::path(storage_dir) / "models" /
                        "test_model" / "v_atomic_001.pending";
    EXPECT_FALSE(std::filesystem::exists(pending_path));

    // Step 2: Simulate interrupt by creating an orphan .pending file
    auto model_dir = std::filesystem::path(storage_dir) / "models" / "test_model";
    std::filesystem::create_directories(model_dir);
    auto orphan_path = model_dir / "v_interrupted.pending";
    {
        std::ofstream out(orphan_path, std::ios::binary);
        out << "partial_interrupted_data";
    }
    ASSERT_TRUE(std::filesystem::exists(orphan_path));

    // Step 3: Run recovery
    auto recover_result = storage.recover_interrupted();
    ASSERT_TRUE(recover_result.ok()) << recover_result.error().message;

    // Step 4: Verify orphan .pending file is cleaned up
    EXPECT_FALSE(std::filesystem::exists(orphan_path));

    // Verify original data is still intact
    auto load2 = storage.load_model("test_model", "v_atomic_001");
    ASSERT_TRUE(load2.ok()) << load2.error().message;
    EXPECT_EQ(load2.value(), "atomic_blob_data");
}

// ---------------------------------------------------------------------------
// Test 8: End-to-end: all v3 components together
// create versions via direct disk writes (bypass cache) -> compact ->
// time travel to compacted version -> verify bias report ->
// build tail_report_v3 -> verify all fields
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, EndToEnd_AllComponents) {
    // ---- Phase 1: Create 15 versions with disk-direct checkpoints ----
    auto versions = create_versions_with_checkpoints("e2e_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // ---- Phase 2: Compact (keep_recent_n=10, compacts 5 oldest) ----
    auto compact_result = compactor.compact("e2e_model");
    ASSERT_TRUE(compact_result.ok()) << compact_result.error().message;
    EXPECT_EQ(compact_result.value().compacted_versions.size(), 5u);
    EXPECT_FALSE(compact_result.value().merged_version_id.empty());

    // Simulate compaction by deleting checkpoint files for compacted versions
    for (const auto& vid : compact_result.value().compacted_versions) {
        auto path = std::filesystem::path(storage_dir) / "models" /
                    "e2e_model" / (vid + ".parquet");
        ASSERT_TRUE(std::filesystem::exists(path))
            << "Checkpoint file for " << vid << " should exist before deletion";
        std::filesystem::remove(path);
    }

    // ---- Phase 3: Time travel to the oldest compacted version ----
    auto& target = versions[0];
    auto tt_result = time_travel.query_as_of("e2e_model",
                                             target.version_id);
    ASSERT_TRUE(tt_result.ok()) << tt_result.error().message;
    EXPECT_TRUE(tt_result.value().was_degraded);

    // ---- Phase 4: Verify bias report ----
    ASSERT_TRUE(tt_result.value().bias_report.has_value());
    const auto& bias = tt_result.value().bias_report.value();
    EXPECT_EQ(bias.requested_version, target.version_id);
    EXPECT_NE(bias.returned_version, target.version_id);
    EXPECT_TRUE(bias.version_mismatch);
    EXPECT_EQ(bias.reason, "compacted");

    // ---- Phase 5: Run alignment to detect drift and get compensation status ----
    AlignmentRequest req;
    req.model_name = "e2e_model";
    req.current_version_id = versions.back().version_id;
    req.current_data = generate_normal(0.0, 1.0, 200, 42);
    req.new_data = generate_normal(8.0, 1.0, 200, 123);  // big shift
    auto align_result = alignment_engine.update_model(req);
    ASSERT_TRUE(align_result.ok()) << align_result.error().message;
    EXPECT_TRUE(align_result.value().drift_detected);

    // ---- Phase 6: Build tail_report_v3 from degraded result ----
    double exclusion_rate = 0.75;  // Simulate high exclusion from compaction
    TailReportV3 report = build_tail_report_v3(
        exclusion_rate,
        tt_result.value().was_degraded,
        align_result.value().compensation_status,
        align_result.value().compensation_deadline);

    EXPECT_EQ(report.rate_band, "high");
    EXPECT_EQ(report.data_grade, "limited_fidelity_conservative");
    EXPECT_TRUE(report.fidelity_mismatch);
    EXPECT_EQ(report.mismatch_reason, "compaction_degraded");
    EXPECT_EQ(report.epistemological_bias, "physical_first");
    EXPECT_TRUE(report.tail_exclusion_statement.find("75%") !=
                std::string::npos);

    // ---- Phase 7: Verify the non-compacted latest version is still exact ----
    auto latest_tt = time_travel.query_as_of("e2e_model",
                                             versions.back().version_id);
    ASSERT_TRUE(latest_tt.ok()) << latest_tt.error().message;
    EXPECT_FALSE(latest_tt.value().was_degraded);
    EXPECT_EQ(latest_tt.value().data, "model_data_v14");
}

// ---------------------------------------------------------------------------
// Test 9: Time travel to a non-compacted version returns exact data
// after compaction has occurred for other versions.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, TimeTravelAfterCompaction_ExactHit) {
    auto versions = create_versions_with_checkpoints("sensor_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // Compact the oldest 5
    auto compact_result = compactor.compact("sensor_model");
    ASSERT_TRUE(compact_result.ok()) << compact_result.error().message;
    EXPECT_EQ(compact_result.value().compacted_versions.size(), 5u);

    // Delete compacted checkpoint files
    for (const auto& vid : compact_result.value().compacted_versions) {
        auto path = std::filesystem::path(storage_dir) / "models" /
                    "sensor_model" / (vid + ".parquet");
        std::filesystem::remove(path);
    }

    // Time-travel to the most recent version -> exact hit, no degradation
    auto& latest = versions[14];
    auto tt_result = time_travel.query_as_of("sensor_model",
                                             latest.version_id);
    ASSERT_TRUE(tt_result.ok()) << tt_result.error().message;
    EXPECT_FALSE(tt_result.value().was_degraded);
    EXPECT_EQ(tt_result.value().data, "model_data_v14");
    EXPECT_FALSE(tt_result.value().bias_report.has_value());
}

// ---------------------------------------------------------------------------
// Test 10: Alignment with convergence then tail_report_v3
// Run enough alignments to reach converged status, then build a tail_report
// with low exclusion rate -> should be statistics_guaranteed.
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, AlignmentConverged_TailReportLowExclusion) {
    std::string prev_id;

    // Run 4 alignments with identical distributions -> converges
    for (int i = 0; i < 4; ++i) {
        AlignmentRequest req;
        req.model_name = "stable_sensor";
        req.current_version_id = prev_id;
        req.current_data = generate_normal(10.0, 2.0, 200, 42);
        req.new_data = generate_normal(10.0, 2.0, 200, 42 + i * 7);
        auto result = alignment_engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
        prev_id = result.value().new_version.version_id;
    }

    // The last alignment should be converged
    AlignmentRequest final_req;
    final_req.model_name = "stable_sensor";
    final_req.current_version_id = prev_id;
    final_req.current_data = generate_normal(10.0, 2.0, 200, 42);
    final_req.new_data = generate_normal(10.0, 2.0, 200, 99);
    auto final_result = alignment_engine.update_model(final_req);
    ASSERT_TRUE(final_result.ok()) << final_result.error().message;
    EXPECT_EQ(final_result.value().compensation_status, "converged");

    // Build tail report with low exclusion rate (no degradation)
    TailReportV3 report = build_tail_report_v3(
        0.15,                               // low exclusion
        false,                              // not degraded
        final_result.value().compensation_status,
        final_result.value().compensation_deadline);

    EXPECT_EQ(report.rate_band, "low");
    EXPECT_EQ(report.data_grade, "statistics_guaranteed");
    EXPECT_EQ(report.compensation_status, "converged");
    EXPECT_FALSE(report.fidelity_mismatch);
}

// ---------------------------------------------------------------------------
// Test 11: Multiple compaction rounds
// Create 15 versions, compact once, then create more, compact again.
// Verify both rounds produce valid compaction results.
// Note: GcCompactor does not remove versions from the chain; it creates a
// merged version and returns the list of compacted version IDs. Chain size
// only grows (originals + merged).
// ---------------------------------------------------------------------------
TEST_F(V3IntegrationTest, MultipleCompactionRounds) {
    // Round 1: create 15 versions, compact
    auto v1 = create_versions_with_checkpoints("multi_model", 15);
    ASSERT_EQ(v1.size(), 15u);

    auto compact1 = compactor.compact("multi_model");
    ASSERT_TRUE(compact1.ok()) << compact1.error().message;
    EXPECT_EQ(compact1.value().compacted_versions.size(), 5u);
    EXPECT_FALSE(compact1.value().merged_version_id.empty());

    // Delete compacted files
    for (const auto& vid : compact1.value().compacted_versions) {
        auto path = std::filesystem::path(storage_dir) / "models" /
                    "multi_model" / (vid + ".parquet");
        std::filesystem::remove(path);
    }

    // Chain has 15 originals + 1 merged = 16
    auto list1 = chain.list_versions("multi_model");
    ASSERT_TRUE(list1.ok());
    EXPECT_EQ(list1.value().size(), 16u);

    // Verify the merged version exists
    auto merged = chain.get_version(compact1.value().merged_version_id);
    ASSERT_TRUE(merged.ok());
    EXPECT_EQ(merged.value()->created_by, "auto_compact");

    // Round 2: add 5 more versions
    std::string parent = v1.back().version_id;
    for (int i = 0; i < 5; ++i) {
        ModelVersion v;
        v.model_name = "multi_model";
        v.fidelity_score = 0.80 - i * 0.01;
        v.training_rows = 3000 + i * 100;
        auto r = chain.create_version("multi_model", parent, v);
        ASSERT_TRUE(r.ok()) << r.error().message;
        write_checkpoint_file(storage_dir, "multi_model",
                              r.value().version_id,
                              "round2_data_v" + std::to_string(i));
        parent = r.value().version_id;
    }

    // Chain has 16 + 5 = 21
    auto list2 = chain.list_versions("multi_model");
    ASSERT_TRUE(list2.ok());
    EXPECT_EQ(list2.value().size(), 21u);

    // Compact again (keep_recent_n=10, so 11 non-recent, minus some
    // already-compacted). Should compact at least 2.
    auto compact2 = compactor.compact("multi_model");
    ASSERT_TRUE(compact2.ok()) << compact2.error().message;
    EXPECT_GE(compact2.value().compacted_versions.size(), 2u);
    EXPECT_FALSE(compact2.value().merged_version_id.empty());

    // Time travel to the first round's compacted version still degrades
    auto tt_result = time_travel.query_as_of("multi_model",
                                             v1[0].version_id);
    ASSERT_TRUE(tt_result.ok()) << tt_result.error().message;
    EXPECT_TRUE(tt_result.value().was_degraded);
}
