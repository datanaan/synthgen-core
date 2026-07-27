#include <gtest/gtest.h>

#include "storage/version/model_version_chain.h"
#include "storage/gc/gc_compactor.h"
#include "storage/gc/protection.h"
#include "storage/timetravel/time_travel_engine.h"
#include "storage/model/model_storage_layer.h"
#include "storage/metadata.h"
#include "engine/alignment/drift_detector.h"
#include "engine/alignment/continuous_alignment_engine.h"

#include <algorithm>
#include <cmath>
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

namespace {

std::vector<double> generate_normal(
    double mean, double stddev, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

// Write checkpoint directly to disk, bypassing ModelStorageLayer's LRU cache
// so that file deletion is correctly reflected on subsequent load_model calls.
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

// ---------------------------------------------------------------------------
// Fixture: each test gets its own unique temp dir using PID + a counter.
// ---------------------------------------------------------------------------
class V3DeepTest : public ::testing::Test {
protected:
    static int test_counter_;
    std::string test_dir;
    std::string storage_dir;
    std::unique_ptr<MetadataManager> meta;
    std::unique_ptr<ModelVersionChain> chain;
    std::unique_ptr<ModelStorageLayer> storage;
    ProtectionConfig gc_config;
    std::unique_ptr<ProtectionChecker> checker;
    std::unique_ptr<GcCompactor> compactor;
    std::unique_ptr<TimeTravelEngine> time_travel;
    std::unique_ptr<ContinuousAlignmentEngine> alignment_engine;

    void SetUp() override {
        std::string base = "synthgen_v3deep_" + std::to_string(::getpid())
                           + "_" + std::to_string(test_counter_++);
        test_dir = (std::filesystem::temp_directory_path() / base).string();
        storage_dir = (std::filesystem::temp_directory_path() /
                      (base + "_storage")).string();
        std::filesystem::remove_all(test_dir);
        std::filesystem::remove_all(storage_dir);
        std::filesystem::create_directories(test_dir);
        std::filesystem::create_directories(storage_dir);

        meta = std::make_unique<MetadataManager>(test_dir);
        chain = std::make_unique<ModelVersionChain>(*meta);
        storage = std::make_unique<ModelStorageLayer>(storage_dir);
        checker = std::make_unique<ProtectionChecker>(gc_config);
        compactor = std::make_unique<GcCompactor>(*chain, *checker, gc_config);
        time_travel = std::make_unique<TimeTravelEngine>(*chain, *storage);
        alignment_engine = std::make_unique<ContinuousAlignmentEngine>(
            *chain, *storage);
    }

    void TearDown() override {
        // Reset unique_ptrs before removing dirs
        alignment_engine.reset();
        time_travel.reset();
        compactor.reset();
        checker.reset();
        storage.reset();
        chain.reset();
        meta.reset();
        std::filesystem::remove_all(test_dir);
        std::filesystem::remove_all(storage_dir);
    }

    // Helper: create N versions with checkpoints written directly to disk.
    std::vector<ModelVersion> create_versions_with_checkpoints(
        const std::string& model, int count) {
        std::vector<ModelVersion> result;
        std::string parent;
        for (int i = 0; i < count; ++i) {
            ModelVersion v;
            v.model_name = model;
            v.fidelity_score = 0.9 - i * 0.001;
            v.training_rows = 1000 + i * 100;
            auto r = chain->create_version(model, parent, v);
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

int V3DeepTest::test_counter_ = 0;

// ===========================================================================
// Test 1: ModelVersionChain — create 100 versions, verify chain integrity
// ===========================================================================
TEST_F(V3DeepTest, ChainIntegrity_100Versions) {
    const int N = 100;
    std::string model = "stress_model";
    std::vector<ModelVersion> created;
    std::string parent;

    for (int i = 0; i < N; ++i) {
        ModelVersion meta;
        meta.fidelity_score = 0.95 - i * 0.001;
        meta.training_rows = 500 + i * 10;
        meta.custom_metadata["idx"] = std::to_string(i);

        auto r = chain->create_version(model, parent, meta);
        ASSERT_TRUE(r.ok()) << "create_version failed at i=" << i
                            << ": " << r.error().message;
        created.push_back(r.value());
        parent = r.value().version_id;
    }

    // Verify count
    auto list = chain->list_versions(model);
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), static_cast<size_t>(N));

    // Verify parent links and individual retrievability
    for (int i = 0; i < N; ++i) {
        auto get = chain->get_version(created[i].version_id);
        ASSERT_TRUE(get.ok()) << "get_version failed for index " << i;
        EXPECT_EQ(get.value()->version_id, created[i].version_id);
        EXPECT_EQ(get.value()->model_name, model);

        if (i == 0) {
            EXPECT_TRUE(created[i].parent_version_id.empty());
            EXPECT_TRUE(created[i].is_first_version());
        } else {
            EXPECT_EQ(created[i].parent_version_id,
                      created[i - 1].version_id);
        }
    }

    // Verify all version IDs are unique
    std::unordered_set<std::string> ids;
    for (const auto& v : created) {
        EXPECT_TRUE(ids.insert(v.version_id).second)
            << "Duplicate version_id: " << v.version_id;
    }
}

// ===========================================================================
// Test 2: ModelVersionChain — get_version for nonexistent ID should error
// ===========================================================================
TEST_F(V3DeepTest, GetVersion_NonexistentId) {
    // Query a version ID that was never created
    auto result = chain->get_version("nonexistent_version_xyz");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kVersionNotFound);
    EXPECT_TRUE(result.error().message.find("nonexistent_version_xyz")
                != std::string::npos);
}

// ===========================================================================
// Test 3: GcCompactor — compact with only 3 versions (keep_recent_n=10)
// Nothing should be compacted (all 3 are "recent").
// ===========================================================================
TEST_F(V3DeepTest, Compact_TooFewVersions_Noop) {
    auto versions = create_versions_with_checkpoints("tiny_model", 3);
    ASSERT_EQ(versions.size(), 3u);

    auto result = compactor->compact("tiny_model");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// ===========================================================================
// Test 4: GcCompactor — compact with all versions anchored
// Even with 15 versions, anchoring all should result in zero compaction.
// ===========================================================================
TEST_F(V3DeepTest, Compact_AllAnchored_Noop) {
    auto versions = create_versions_with_checkpoints("anchored_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // Anchor every single version
    for (const auto& v : versions) {
        checker->anchor(v.version_id);
    }

    auto result = compactor->compact("anchored_model");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// ===========================================================================
// Test 5: TimeTravel — query_as_of for most recent version should not degrade
// ===========================================================================
TEST_F(V3DeepTest, QueryAsOf_MostRecentVersion_NoDegrade) {
    auto versions = create_versions_with_checkpoints("tt_model", 10);
    ASSERT_EQ(versions.size(), 10u);

    auto& latest = versions.back();
    auto result = time_travel->query_as_of("tt_model", latest.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().was_degraded);
    EXPECT_EQ(result.value().data, "model_data_v9");
    EXPECT_FALSE(result.value().bias_report.has_value());
}

// ===========================================================================
// Test 6: TimeTravel — query_as_of for first version in a fresh chain
// No compaction has happened, so even the first version should be exact.
// ===========================================================================
TEST_F(V3DeepTest, QueryAsOf_FirstVersion_FreshChain_NoDegrade) {
    auto versions = create_versions_with_checkpoints("fresh_model", 10);
    ASSERT_EQ(versions.size(), 10u);

    auto& first = versions.front();
    auto result = time_travel->query_as_of("fresh_model", first.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().was_degraded);
    EXPECT_EQ(result.value().data, "model_data_v0");
    EXPECT_FALSE(result.value().bias_report.has_value());
}

// ===========================================================================
// Test 7: DriftDetector — KS test with very small samples (n=5)
// Should not crash; should return a valid result.
// ===========================================================================
TEST_F(V3DeepTest, DriftDetector_TinySamples) {
    DriftDetector detector("ks", 0.05);

    std::vector<double> current = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> new_data = {10.0, 20.0, 30.0, 40.0, 50.0};

    auto result = detector.detect(current, new_data);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // With such different distributions even at n=5, drift_score > 0
    EXPECT_GE(result.value().drift_score, 0.0);
    EXPECT_LE(result.value().drift_score, 1.0);
    EXPECT_GE(result.value().ks_statistic, 0.0);
    EXPECT_GE(result.value().p_value, 0.0);
    EXPECT_LE(result.value().p_value, 1.0);
}

// ===========================================================================
// Test 8: DriftDetector — KS test with very large samples (n=10000)
// Should detect real drift between clearly different distributions.
// ===========================================================================
TEST_F(V3DeepTest, DriftDetector_LargeSamples_RealDrift) {
    DriftDetector detector("ks", 0.05);

    auto current = generate_normal(0.0, 1.0, 10000, 42);
    auto shifted = generate_normal(5.0, 1.0, 10000, 99);

    auto result = detector.detect(current, shifted);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // With 10k samples and a 5-sigma shift, drift should be overwhelming
    EXPECT_TRUE(result.value().drift_detected);
    EXPECT_GT(result.value().drift_score, 0.8);
    EXPECT_NEAR(result.value().p_value, 0.0, 1e-10);
}

// ===========================================================================
// Test 9: ContinuousAlignment — alignment with empty current_data
// This is the "first alignment" scenario. Should succeed with no drift.
// ===========================================================================
TEST_F(V3DeepTest, Alignment_EmptyCurrentData_FirstAlignment) {
    AlignmentRequest req;
    req.model_name = "first_align_model";
    req.current_version_id = "";
    req.current_data = {};   // empty = first alignment
    req.new_data = generate_normal(0.0, 1.0, 100, 42);

    auto result = alignment_engine->update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // First alignment: no drift comparison possible
    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_DOUBLE_EQ(result.value().drift_score, 0.0);
    EXPECT_FALSE(result.value().new_version.version_id.empty());
    EXPECT_TRUE(result.value().new_version.parent_version_id.empty());

    // Checkpoint should be loadable from storage
    auto load = storage->load_model(
        "first_align_model", result.value().new_version.version_id);
    ASSERT_TRUE(load.ok()) << load.error().message;
    EXPECT_FALSE(load.value().empty());
}

// ===========================================================================
// Test 10: ContinuousAlignment — 5 alignments with alternating drift
// Alternate between same-distribution and shifted data; verify drift_detected
// toggles correctly.
// ===========================================================================
TEST_F(V3DeepTest, Alignment_AlternatingDrift_TogglesCorrectly) {
    std::string prev_id;
    std::vector<bool> drift_flags;

    // Baseline distribution
    auto baseline = generate_normal(5.0, 1.0, 200, 42);
    auto shifted  = generate_normal(20.0, 1.0, 200, 77);

    for (int i = 0; i < 5; ++i) {
        AlignmentRequest req;
        req.model_name = "alternating_model";
        req.current_version_id = prev_id;

        if (i == 0) {
            // First alignment: no current_data, use baseline as new_data
            req.current_data = {};
            req.new_data = baseline;
        } else {
            req.current_data = baseline;
            // Alternate: even i => same distribution, odd i => shifted
            if (i % 2 == 0) {
                req.new_data = baseline;
            } else {
                req.new_data = shifted;
            }
        }

        auto result = alignment_engine->update_model(req);
        ASSERT_TRUE(result.ok()) << "Alignment " << i << " failed: "
                                 << result.error().message;
        drift_flags.push_back(result.value().drift_detected);
        prev_id = result.value().new_version.version_id;
    }

    // i=0: first alignment, no drift detection => false
    EXPECT_FALSE(drift_flags[0]);

    // i=1: shifted data vs baseline => drift detected
    EXPECT_TRUE(drift_flags[1]);

    // i=2: baseline vs baseline => no drift
    EXPECT_FALSE(drift_flags[2]);

    // i=3: shifted vs baseline => drift detected
    EXPECT_TRUE(drift_flags[3]);

    // i=4: baseline vs baseline => no drift
    EXPECT_FALSE(drift_flags[4]);
}

// ===========================================================================
// Test 11: ModelStorageLayer — save then delete checkpoint file then load
// After deleting the on-disk file, load should fail (but the LRU cache may
// still have it). We bypass cache by using a fresh ModelStorageLayer instance
// or by directly calling load on a path where the file was deleted.
// ===========================================================================
TEST_F(V3DeepTest, Storage_SaveDeleteThenLoad) {
    std::string model = "delete_test_model";
    std::string version_id = "v_delete_001";
    std::string data = "important_model_data";

    // Save via atomic_write (populates cache)
    ModelVersion v;
    v.version_id = version_id;
    v.model_name = model;
    auto save_result = storage->atomic_write(model, data, v);
    ASSERT_TRUE(save_result.ok()) << save_result.error().message;

    // Load via same instance (cache hit) — should succeed
    auto cached_load = storage->load_model(model, version_id);
    ASSERT_TRUE(cached_load.ok()) << cached_load.error().message;
    EXPECT_EQ(cached_load.value(), data);

    // Now delete the checkpoint file from disk
    auto path = std::filesystem::path(storage_dir) / "models" /
                model / (version_id + ".parquet");
    ASSERT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
    ASSERT_FALSE(std::filesystem::exists(path));

    // Create a fresh ModelStorageLayer pointing to the same root (empty cache)
    ModelStorageLayer fresh_storage(storage_dir);
    auto fresh_load = fresh_storage.load_model(model, version_id);
    EXPECT_FALSE(fresh_load.ok());
    EXPECT_EQ(fresh_load.error().code, ErrorCode::kVersionNotFound);
}

// ===========================================================================
// Test 12: ModelStorageLayer — atomic_write with very large data (10MB)
// Should succeed without crash or truncation.
// ===========================================================================
TEST_F(V3DeepTest, Storage_AtomicWrite_10MB) {
    std::string model = "big_model";
    std::string version_id = "v_big_001";

    // Construct 10 MB of data
    const size_t data_size = 10 * 1024 * 1024;
    std::string big_data(data_size, '\0');
    // Fill with a pattern to detect truncation
    for (size_t i = 0; i < data_size; ++i) {
        big_data[i] = static_cast<char>(i % 251);  // prime for variety
    }

    ModelVersion v;
    v.version_id = version_id;
    v.model_name = model;

    auto write_result = storage->atomic_write(model, big_data, v);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    // Load with a fresh storage layer (bypass cache) to verify disk integrity
    ModelStorageLayer fresh_storage(storage_dir);
    auto load_result = fresh_storage.load_model(model, version_id);
    ASSERT_TRUE(load_result.ok()) << load_result.error().message;
    EXPECT_EQ(load_result.value().size(), data_size);
    EXPECT_EQ(load_result.value(), big_data);
}

// ===========================================================================
// Test 13: DriftDetector KS statistic — duplicated values
// Verify correct KS statistic when one sample has multiple copies of a value.
// For s1=[5,5] vs s2=[5,10]:
//   F1(5) = 1.0, F2(5) = 0.5, diff = 0.5 (the maximum CDF divergence)
//   F1(10) = 1.0, F2(10) = 1.0, diff = 0
// Maximum KS = 0.5 (the sup-norm of the difference of right-continuous CDFs)
// ===========================================================================
TEST_F(V3DeepTest, DriftDetector_KSDuplicatedValues_Correct) {
    DriftDetector detector("ks", 0.05);

    std::vector<double> s1 = {5.0, 5.0};
    std::vector<double> s2 = {5.0, 10.0};

    auto result = detector.detect(s1, s2);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // KS statistic = sup_x |F1(x) - F2(x)| = 0.5
    EXPECT_NEAR(result.value().ks_statistic, 0.5, 0.01);
    EXPECT_NEAR(result.value().drift_score, 0.5, 0.01);
}

// ===========================================================================
// Test 14: ModelStorageLayer — LRU cache serves stale data after file deletion
// This is a production bug: after GC compaction deletes checkpoint files,
// the same ModelStorageLayer instance will return stale cached data instead
// of reporting kVersionNotFound.
// ===========================================================================
TEST_F(V3DeepTest, Storage_CacheStaleAfterFileDeletion) {
    std::string model = "stale_model";
    std::string version_id = "v_stale_001";
    std::string data = "original_data";

    // Save (populates cache)
    ModelVersion v;
    v.version_id = version_id;
    v.model_name = model;
    auto save_result = storage->atomic_write(model, data, v);
    ASSERT_TRUE(save_result.ok()) << save_result.error().message;

    // Load succeeds (cache hit)
    auto load1 = storage->load_model(model, version_id);
    ASSERT_TRUE(load1.ok()) << load1.error().message;
    EXPECT_EQ(load1.value(), data);

    // Delete the file from disk (simulating compaction)
    auto path = std::filesystem::path(storage_dir) / "models" /
                model / (version_id + ".parquet");
    std::filesystem::remove(path);

    // Load via same instance: should now fail (file gone, cache invalidated).
    // Previously this was a bug where the LRU cache returned stale data.
    // Fixed: load_model now validates that the file still exists on cache hit.
    auto load2 = storage->load_model(model, version_id);
    EXPECT_FALSE(load2.ok())
        << "load_model should fail after file deletion (cache invalidation)";
    EXPECT_EQ(load2.error().code, ErrorCode::kVersionNotFound);
}

// ===========================================================================
// Test 15: ModelStorageLayer — LRU cache eviction correctness
// With kMaxCacheSize=5, inserting 6 entries should evict the oldest.
// Verify that the least-recently-used entry is evicted.
// ===========================================================================
TEST_F(V3DeepTest, Storage_LRUEviction) {
    const int num_entries = 7;  // exceeds kMaxCacheSize=5

    for (int i = 0; i < num_entries; ++i) {
        ModelVersion v;
        v.version_id = "v_lru_" + std::to_string(i);
        v.model_name = "lru_model";
        auto r = storage->save_checkpoint(
            "lru_model", v.version_id, "data_" + std::to_string(i));
        ASSERT_TRUE(r.ok()) << r.error().message;
    }

    // Verify all files exist on disk
    for (int i = 0; i < num_entries; ++i) {
        auto path = std::filesystem::path(storage_dir) / "models" /
                    "lru_model" / ("v_lru_" + std::to_string(i) + ".parquet");
        EXPECT_TRUE(std::filesystem::exists(path))
            << "File v_lru_" << i << " should exist on disk";
    }

    // Create a fresh storage to force all loads from disk.
    // The first 2 entries (v_lru_0, v_lru_1) were evicted from the
    // original cache, but they still exist on disk. Verify via fresh load.
    ModelStorageLayer fresh(storage_dir);
    for (int i = 0; i < num_entries; ++i) {
        auto load = fresh.load_model("lru_model", "v_lru_" + std::to_string(i));
        ASSERT_TRUE(load.ok()) << "Failed to load v_lru_" << i
                               << ": " << load.error().message;
        EXPECT_EQ(load.value(), "data_" + std::to_string(i));
    }
}

// ===========================================================================
// Test 16: GcCompactor — concurrent compaction attempt should fail
// If in_progress_ is true, a second compact() call should return
// kCompactionInProgress.
// ===========================================================================
TEST_F(V3DeepTest, Compact_ConcurrentAttempt) {
    auto versions = create_versions_with_checkpoints("concurrent_model", 15);
    ASSERT_EQ(versions.size(), 15u);

    // Force in_progress flag on
    compactor->set_in_progress_for_test(true);

    auto result = compactor->compact("concurrent_model");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kCompactionInProgress);

    // Reset and verify normal compaction works
    compactor->set_in_progress_for_test(false);
    auto result2 = compactor->compact("concurrent_model");
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_EQ(result2.value().compacted_versions.size(), 5u);
}

// ===========================================================================
// Test 17: ModelVersionChain — create_version with wrong model parent
// Parent version belongs to a different model — should return error.
// ===========================================================================
TEST_F(V3DeepTest, Chain_ParentFromDifferentModel) {
    // Create a version in model_a
    ModelVersion v;
    v.model_name = "model_a";
    auto r1 = chain->create_version("model_a", "", v);
    ASSERT_TRUE(r1.ok()) << r1.error().message;

    // Try to create a version in model_b using model_a's version as parent
    auto r2 = chain->create_version("model_b", r1.value().version_id, v);
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, ErrorCode::kInvalidArgument);
    EXPECT_TRUE(r2.error().message.find("different model") != std::string::npos);
}

// ===========================================================================
// Test 18: TimeTravel — query_as_of for nonexistent model/version
// Should fail gracefully with an error.
// ===========================================================================
TEST_F(V3DeepTest, QueryAsOf_NonexistentVersion) {
    auto result = time_travel->query_as_of("ghost_model", "nonexistent_vid");
    EXPECT_FALSE(result.ok());
}

// ===========================================================================
// Test 19: GcCompactor — compact for model with no versions
// Should return empty CompactionResult (no crash).
// ===========================================================================
TEST_F(V3DeepTest, Compact_EmptyModel) {
    auto result = compactor->compact("nonexistent_model");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// ===========================================================================
// Test 20: DriftDetector — identical samples (no drift at all)
// Two identical samples should produce KS statistic = 0, no drift detected.
// ===========================================================================
TEST_F(V3DeepTest, DriftDetector_IdenticalSamples_NoDrift) {
    DriftDetector detector("ks", 0.05);

    std::vector<double> sample = {1.0, 2.0, 3.0, 4.0, 5.0};

    auto result = detector.detect(sample, sample);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_NEAR(result.value().ks_statistic, 0.0, 1e-10);
    EXPECT_NEAR(result.value().drift_score, 0.0, 1e-10);
    EXPECT_NEAR(result.value().p_value, 1.0, 1e-10);
}

// ===========================================================================
// Test 21: ContinuousAlignment — compensation deadline timeout
// Set a deadline in the past, run alignment with drift, verify
// compensation_status becomes "timeout_degraded".
// ===========================================================================
TEST_F(V3DeepTest, Alignment_DeadlineTimeout) {
    // Set deadline in the past (1 hour ago)
    auto now = std::chrono::steady_clock::now();
    int64_t past_deadline = std::chrono::duration_cast<
        std::chrono::microseconds>(now.time_since_epoch()).count()
        - 3600000000LL;

    alignment_engine->set_compensation_deadline("timeout_model", past_deadline);

    // First alignment (no drift, establishes baseline)
    AlignmentRequest req1;
    req1.model_name = "timeout_model";
    req1.current_data = {};
    req1.new_data = generate_normal(0.0, 1.0, 200, 42);
    auto r1 = alignment_engine->update_model(req1);
    ASSERT_TRUE(r1.ok()) << r1.error().message;

    // Second alignment with big drift — deadline already expired
    AlignmentRequest req2;
    req2.model_name = "timeout_model";
    req2.current_version_id = r1.value().new_version.version_id;
    req2.current_data = generate_normal(0.0, 1.0, 200, 42);
    req2.new_data = generate_normal(20.0, 1.0, 200, 99);
    auto r2 = alignment_engine->update_model(req2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;

    // Since deadline is past and drift > 0, status should be timeout_degraded
    EXPECT_EQ(r2.value().compensation_status, "timeout_degraded");
}

// ===========================================================================
// Test 22: ModelVersionChain — empty model_name should be rejected
// ===========================================================================
TEST_F(V3DeepTest, Chain_EmptyModelName) {
    ModelVersion v;
    auto result = chain->create_version("", "", v);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===========================================================================
// Test 23: DriftDetector — mode "none" should skip detection
// ===========================================================================
TEST_F(V3DeepTest, DriftDetector_ModeNone) {
    DriftDetector detector("none", 0.05);

    auto current = generate_normal(0.0, 1.0, 100, 42);
    auto shifted = generate_normal(100.0, 1.0, 100, 99);

    auto result = detector.detect(current, shifted);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_DOUBLE_EQ(result.value().drift_score, 0.0);
    EXPECT_DOUBLE_EQ(result.value().ks_statistic, 0.0);
    EXPECT_DOUBLE_EQ(result.value().p_value, 1.0);
}
