#include <gtest/gtest.h>

#include "storage/gc/gc_compactor.h"
#include "storage/gc/protection.h"
#include "storage/version/model_version_chain.h"
#include "storage/metadata.h"

#include <filesystem>
#include <unistd.h>
#include <vector>

using namespace synthgen;
using namespace synthgen::storage;
using namespace synthgen::storage::version;
using namespace synthgen::storage::gc;

class GcCompactorTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_GC_" + std::to_string(::getpid()))).string();
    MetadataManager meta{test_dir};
    ModelVersionChain chain{meta};
    ProtectionConfig config;
    ProtectionChecker checker{config};

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    std::vector<ModelVersion> create_versions(
        const std::string& model, int count) {
        std::vector<ModelVersion> result;
        std::string parent;
        ModelVersion v;
        v.model_name = model;
        for (int i = 0; i < count; ++i) {
            v.fidelity_score = 0.9 - i * 0.01;
            v.training_rows = 1000 + i * 100;
            auto r = chain.create_version(model, parent, v);
            if (r.ok()) {
                result.push_back(r.value());
                parent = r.value().version_id;
            }
        }
        return result;
    }
};

// 1. Compact two oldest versions when keep_recent_n=10 and 12 versions exist
TEST_F(GcCompactorTest, CompactTwoVersions_MergedCorrectly) {
    auto versions = create_versions("sensor_a", 12);
    ASSERT_EQ(versions.size(), 12u);

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_a");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    // Should compact at least 2 (the 2 oldest, outside top 10)
    EXPECT_GE(cr.compacted_versions.size(), 2u);
    EXPECT_FALSE(cr.merged_version_id.empty());

    // The compacted versions should be the oldest ones
    EXPECT_EQ(cr.compacted_versions[0], versions[0].version_id);
    EXPECT_EQ(cr.compacted_versions[1], versions[1].version_id);
}

// 2. A snapshot-referenced version is protected from compaction
TEST_F(GcCompactorTest, SnapshotRef_PreventsCompaction) {
    auto versions = create_versions("sensor_b", 12);
    ASSERT_EQ(versions.size(), 12u);

    // Protect the oldest version with a snapshot reference
    checker.add_snapshot_ref(versions[0].version_id);

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_b");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    // Only version[1] is compactable (version[0] is snapshot-protected),
    // which is fewer than 2, so no compaction happens
    // Wait: with 12 versions, keep_recent_n=10 means versions[0] and [1]
    // are outside recent. If [0] is protected, only [1] is compactable (<2).
    EXPECT_TRUE(cr.compacted_versions.empty());
    EXPECT_TRUE(cr.merged_version_id.empty());
}

// 3. An anchored version is protected from compaction
TEST_F(GcCompactorTest, Anchored_PreventsCompaction) {
    auto versions = create_versions("sensor_c", 12);
    ASSERT_EQ(versions.size(), 12u);

    // Anchor the oldest version
    checker.anchor(versions[0].version_id);

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_c");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    // Only version[1] is compactable, so <2 → empty result
    EXPECT_TRUE(cr.compacted_versions.empty());
    EXPECT_TRUE(cr.merged_version_id.empty());
}

// 4. Versions within keep_recent_n are protected
TEST_F(GcCompactorTest, WithinNVersions_Protected) {
    auto versions = create_versions("sensor_d", 12);
    ASSERT_EQ(versions.size(), 12u);

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_d");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    // Only versions[0] and [1] (the 2 oldest) are outside the recent 10
    // Both should be compacted
    EXPECT_EQ(cr.compacted_versions.size(), 2u);

    // Verify none of the recent 10 were compacted
    for (int i = 2; i < 12; ++i) {
        EXPECT_TRUE(std::find(cr.compacted_versions.begin(),
                              cr.compacted_versions.end(),
                              versions[i].version_id)
                    == cr.compacted_versions.end())
            << "Recent version " << i << " should not be compacted";
    }
}

// 5. Merged version fidelity is min of originals
TEST_F(GcCompactorTest, MetadataMerge_FidelityIsMin) {
    auto versions = create_versions("sensor_e", 12);
    ASSERT_EQ(versions.size(), 12u);

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_e");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    ASSERT_FALSE(cr.merged_version_id.empty());

    // Fetch the merged version
    auto merged = chain.get_version(cr.merged_version_id);
    ASSERT_TRUE(merged.ok()) << merged.error().message;

    // Fidelity should be min of compacted versions
    // versions[0].fidelity_score = 0.9, versions[1].fidelity_score = 0.89
    // min = 0.89
    double expected_min = std::min(versions[0].fidelity_score,
                                    versions[1].fidelity_score);
    EXPECT_DOUBLE_EQ(merged.value()->fidelity_score, expected_min);

    // training_rows should be sum
    int64_t expected_rows = versions[0].training_rows + versions[1].training_rows;
    EXPECT_EQ(merged.value()->training_rows, expected_rows);

    // created_by should be auto_compact
    EXPECT_EQ(merged.value()->created_by, "auto_compact");
}

// 6. Fewer than keep_recent_n versions → nothing compactable
TEST_F(GcCompactorTest, NoCompactableVersions_EmptyResult) {
    auto versions = create_versions("sensor_f", 5);
    ASSERT_EQ(versions.size(), 5u);

    // keep_recent_n = 10, so all 5 are within recent N → nothing to compact
    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_f");
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// 7. All versions protected → empty result
TEST_F(GcCompactorTest, AllProtected_EmptyResult) {
    auto versions = create_versions("sensor_g", 12);
    ASSERT_EQ(versions.size(), 12u);

    // Anchor everything
    for (const auto& v : versions) {
        checker.anchor(v.version_id);
    }

    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("sensor_g");
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// 8. Explain shows correct compactable count
TEST_F(GcCompactorTest, Explain_ShowsCompactableCount) {
    create_versions("sensor_h", 12);

    GcCompactor compactor(chain, checker, config);
    auto info = compactor.explain("sensor_h");

    EXPECT_EQ(info.total_versions, 12);
    EXPECT_EQ(info.protected_versions, 10);  // recent 10
    EXPECT_EQ(info.compactable_versions, 2);  // oldest 2
}

// 9. Concurrent compaction returns error
TEST_F(GcCompactorTest, ConcurrentCompaction_ReturnsError) {
    create_versions("sensor_i", 12);

    GcCompactor compactor(chain, checker, config);
    compactor.set_in_progress_for_test(true);

    auto result = compactor.compact("sensor_i");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kCompactionInProgress);

    // Clean up
    compactor.set_in_progress_for_test(false);
}

// 10. Nonexistent model returns empty result
TEST_F(GcCompactorTest, ModelNotFound_EmptyResult) {
    GcCompactor compactor(chain, checker, config);
    auto result = compactor.compact("nonexistent_model");
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// 11. keep_recent_n equals total → no compaction
TEST_F(GcCompactorTest, KeepRecentN_EqualsTotal_NoCompaction) {
    auto versions = create_versions("sensor_j", 15);
    ASSERT_EQ(versions.size(), 15u);

    ProtectionConfig cfg;
    cfg.keep_recent_n = 15;
    ProtectionChecker prot{cfg};

    GcCompactor compactor(chain, prot, cfg);
    auto result = compactor.compact("sensor_j");
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_TRUE(result.value().compacted_versions.empty());
    EXPECT_TRUE(result.value().merged_version_id.empty());
}

// 12. keep_recent_n=0 means all versions are compactable
TEST_F(GcCompactorTest, KeepRecentN_Zero_AllCompactable) {
    auto versions = create_versions("sensor_k", 3);
    ASSERT_EQ(versions.size(), 3u);

    ProtectionConfig cfg;
    cfg.keep_recent_n = 0;
    ProtectionChecker prot{cfg};

    GcCompactor compactor(chain, prot, cfg);
    auto result = compactor.compact("sensor_k");
    ASSERT_TRUE(result.ok()) << result.error().message;
    const auto& cr = result.value();

    EXPECT_EQ(cr.compacted_versions.size(), 3u);
    EXPECT_FALSE(cr.merged_version_id.empty());
}

// 13. Auto compact disabled returns error
TEST_F(GcCompactorTest, AutoCompactDisabled_ReturnsError) {
    ProtectionConfig cfg;
    cfg.auto_compact_enabled = false;
    ProtectionChecker prot{cfg};

    GcCompactor compactor(chain, prot, cfg);
    auto result = compactor.auto_compact_check();

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kAutoCompactDisabled);
}

// 14. Metadata merge preserves custom_metadata from all compacted versions
TEST_F(GcCompactorTest, MetadataMerge_CustomMetadataPreserved) {
    ModelVersion v1_meta;
    v1_meta.model_name = "sensor_l";
    v1_meta.custom_metadata["source"] = "run_A";
    auto r1 = chain.create_version("sensor_l", "", v1_meta);
    ASSERT_TRUE(r1.ok());

    ModelVersion v2_meta;
    v2_meta.model_name = "sensor_l";
    v2_meta.custom_metadata["source"] = "run_B";
    v2_meta.custom_metadata["extra"] = "data";
    auto r2 = chain.create_version("sensor_l", r1.value().version_id, v2_meta);
    ASSERT_TRUE(r2.ok());

    // Create 10 more to push v1 and v2 outside keep_recent_n
    std::string parent = r2.value().version_id;
    for (int i = 0; i < 10; ++i) {
        ModelVersion v;
        v.model_name = "sensor_l";
        v.fidelity_score = 0.95;
        auto r = chain.create_version("sensor_l", parent, v);
        ASSERT_TRUE(r.ok());
        parent = r.value().version_id;
    }

    ProtectionConfig cfg;
    cfg.keep_recent_n = 10;
    ProtectionChecker prot{cfg};
    GcCompactor compactor(chain, prot, cfg);

    auto result = compactor.compact("sensor_l");
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_FALSE(result.value().merged_version_id.empty());

    auto merged = chain.get_version(result.value().merged_version_id);
    ASSERT_TRUE(merged.ok());
    // Latter overwrites: source should be "run_B"
    EXPECT_EQ(merged.value()->custom_metadata.at("source"), "run_B");
    // Extra key preserved
    EXPECT_EQ(merged.value()->custom_metadata.at("extra"), "data");
}

// 15. Compaction result includes both compacted version IDs
TEST_F(GcCompactorTest, CompactionResult_AllCompactedVersionsListed) {
    ProtectionConfig cfg;
    cfg.keep_recent_n = 5;
    ProtectionChecker prot{cfg};

    auto versions = create_versions("sensor_m", 8);
    ASSERT_EQ(versions.size(), 8u);

    GcCompactor compactor(chain, prot, cfg);
    auto result = compactor.compact("sensor_m");
    ASSERT_TRUE(result.ok()) << result.error().message;

    // 3 versions (indices 0,1,2) should be outside recent 5
    EXPECT_EQ(result.value().compacted_versions.size(), 3u);

    // Verify each compacted version ID appears exactly once
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(result.value().compacted_versions[i],
                  versions[i].version_id);
    }

    // Merged version should exist in the chain
    auto merged = chain.get_version(result.value().merged_version_id);
    ASSERT_TRUE(merged.ok());
    EXPECT_EQ(merged.value()->created_by, "auto_compact");
}
