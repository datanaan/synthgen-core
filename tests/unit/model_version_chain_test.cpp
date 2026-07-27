#include <gtest/gtest.h>

#include "storage/version/model_version_chain.h"
#include "storage/metadata.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

class ModelVersionChainTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_VERSION_" + std::to_string(::getpid()))).string();

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        meta_ = std::make_unique<synthgen::storage::MetadataManager>(
            test_dir);
        chain_ = std::make_unique<
            synthgen::storage::version::ModelVersionChain>(*meta_);
    }

    void TearDown() override {
        chain_.reset();
        meta_.reset();
        std::filesystem::remove_all(test_dir);
    }

    std::unique_ptr<synthgen::storage::MetadataManager> meta_;
    std::unique_ptr<synthgen::storage::version::ModelVersionChain> chain_;
};

// Helper to create a blank ModelVersion for metadata
synthgen::storage::version::ModelVersion blank_meta() {
    return {};
}

}  // namespace

// ===== Functional Tests =====

// Test 1: Create first version (no parent) — success
TEST_F(ModelVersionChainTest, CreateFirstVersionNoParent) {
    auto result = chain_->create_version("sensor_model", "", blank_meta());
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().version_id.empty());
    EXPECT_EQ(result.value().model_name, "sensor_model");
    EXPECT_TRUE(result.value().parent_version_id.empty());
    EXPECT_TRUE(result.value().is_first_version());
    EXPECT_TRUE(result.value().is_immutable);
    EXPECT_GT(result.value().created_at, 0);
    EXPECT_EQ(result.value().created_by, "user");
}

// Test 2: Create child version with valid parent — success
TEST_F(ModelVersionChainTest, CreateChildVersionWithValidParent) {
    auto v1 = chain_->create_version("sensor_model", "", blank_meta());
    ASSERT_TRUE(v1.ok()) << v1.error().message;

    auto v2 = chain_->create_version("sensor_model", v1.value().version_id,
                                      blank_meta());
    ASSERT_TRUE(v2.ok()) << v2.error().message;
    EXPECT_EQ(v2.value().parent_version_id, v1.value().version_id);
    EXPECT_FALSE(v2.value().is_first_version());
    EXPECT_EQ(v2.value().model_name, "sensor_model");
}

// Test 3: Get existing version — returns correct data
TEST_F(ModelVersionChainTest, GetExistingVersion) {
    synthgen::storage::version::ModelVersion meta;
    meta.fidelity_score = 0.95;
    meta.training_rows = 10000;
    meta.training_data_range = "2024-01-01:2024-12-31";

    auto created = chain_->create_version("model_a", "", meta);
    ASSERT_TRUE(created.ok());

    auto retrieved = chain_->get_version(created.value().version_id);
    ASSERT_TRUE(retrieved.ok()) << retrieved.error().message;
    EXPECT_EQ(retrieved.value()->version_id, created.value().version_id);
    EXPECT_EQ(retrieved.value()->model_name, "model_a");
    EXPECT_DOUBLE_EQ(retrieved.value()->fidelity_score, 0.95);
    EXPECT_EQ(retrieved.value()->training_rows, 10000);
    EXPECT_EQ(retrieved.value()->training_data_range,
              "2024-01-01:2024-12-31");
}

// Test 4: List versions — multiple versions sorted descending by time
TEST_F(ModelVersionChainTest, ListVersionsSortedDescending) {
    chain_->create_version("model_a", "", blank_meta());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    chain_->create_version("model_a", "", blank_meta());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    chain_->create_version("model_a", "", blank_meta());

    auto result = chain_->list_versions("model_a");
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().size(), 3u);

    // Should be sorted descending by created_at
    EXPECT_GE(result.value()[0].created_at, result.value()[1].created_at);
    EXPECT_GE(result.value()[1].created_at, result.value()[2].created_at);
}

// Test 5: List versions with limit — returns correct count
TEST_F(ModelVersionChainTest, ListVersionsWithLimit) {
    for (int i = 0; i < 5; ++i) {
        auto r = chain_->create_version("model_b", "", blank_meta());
        ASSERT_TRUE(r.ok()) << r.error().message;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto result = chain_->list_versions("model_b", 3);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().size(), 3u);
}

// Test 6: List versions for nonexistent model — empty list
TEST_F(ModelVersionChainTest, ListVersionsNonexistentModel) {
    auto result = chain_->list_versions("nonexistent");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().empty());
}

// Test 7: Modify version — always returns kImmutableViolation
TEST_F(ModelVersionChainTest, ModifyVersionAlwaysFails) {
    auto created = chain_->create_version("model_a", "", blank_meta());
    ASSERT_TRUE(created.ok());

    auto result = chain_->modify_version(created.value().version_id);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              synthgen::ErrorCode::kImmutableViolation);
}

// Test 8: Custom metadata stored correctly
TEST_F(ModelVersionChainTest, CustomMetadataStoredCorrectly) {
    synthgen::storage::version::ModelVersion meta;
    meta.custom_metadata["experiment_id"] = "exp_42";
    meta.custom_metadata["notes"] = "test run";

    auto created = chain_->create_version("model_a", "", meta);
    ASSERT_TRUE(created.ok());

    auto retrieved = chain_->get_version(created.value().version_id);
    ASSERT_TRUE(retrieved.ok());
    EXPECT_EQ(retrieved.value()->custom_metadata.at("experiment_id"), "exp_42");
    EXPECT_EQ(retrieved.value()->custom_metadata.at("notes"), "test run");
}

// Test 9: Two versions get different version_ids
TEST_F(ModelVersionChainTest, TwoVersionsDifferentIds) {
    auto v1 = chain_->create_version("model_a", "", blank_meta());
    auto v2 = chain_->create_version("model_a", "", blank_meta());
    ASSERT_TRUE(v1.ok());
    ASSERT_TRUE(v2.ok());
    EXPECT_NE(v1.value().version_id, v2.value().version_id);
}

// Test 10: Audit/metrics not crashing (basic smoke)
TEST_F(ModelVersionChainTest, MetricsSmokeTest) {
    synthgen::scaffold::SpanGuard::active_spans().clear();
    synthgen::scaffold::MetricsRegistry::instance().reset();

    chain_->create_version("model_a", "", blank_meta());

    // Metrics should have been recorded
    auto counters =
        synthgen::scaffold::MetricsRegistry::instance().all_counters();
    EXPECT_GT(counters.size(), 0u);

    // Trace spans should have been created
    bool found = false;
    for (const auto& sp : synthgen::scaffold::SpanGuard::active_spans()) {
        if (sp.component == "version") found = true;
    }
    EXPECT_TRUE(found);
}

// Test 11: created_by field defaults to "user" and accepts custom values
TEST_F(ModelVersionChainTest, CreatedByField) {
    synthgen::storage::version::ModelVersion meta_user;
    auto v1 = chain_->create_version("model_a", "", meta_user);
    ASSERT_TRUE(v1.ok());
    EXPECT_EQ(v1.value().created_by, "user");

    synthgen::storage::version::ModelVersion meta_system;
    meta_system.created_by = "system";
    auto v2 = chain_->create_version("model_a", "", meta_system);
    ASSERT_TRUE(v2.ok());
    EXPECT_EQ(v2.value().created_by, "system");
}

// ===== Error Tests =====

// Test 12: Get nonexistent version -> kVersionNotFound
TEST_F(ModelVersionChainTest, GetNonexistentVersion) {
    auto result = chain_->get_version("nonexistent_id");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              synthgen::ErrorCode::kVersionNotFound);
}

// Test 13: Create with nonexistent parent -> kParentNotFound
TEST_F(ModelVersionChainTest, CreateWithNonexistentParent) {
    auto result =
        chain_->create_version("model_a", "nonexistent_parent", blank_meta());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              synthgen::ErrorCode::kParentNotFound);
}

// Test 14: Create with empty model_name -> kInvalidArgument
TEST_F(ModelVersionChainTest, CreateWithEmptyModelName) {
    auto result = chain_->create_version("", "", blank_meta());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              synthgen::ErrorCode::kInvalidArgument);
}

// Test 15: Chain depth 50 works
TEST_F(ModelVersionChainTest, ChainDepth50Works) {
    std::string parent_id;
    for (int i = 0; i < 50; ++i) {
        auto result =
            chain_->create_version("deep_model", parent_id, blank_meta());
        ASSERT_TRUE(result.ok()) << "Failed at depth " << i << ": "
                                  << result.error().message;
        parent_id = result.value().version_id;
    }

    // Verify the last version
    auto last = chain_->get_version(parent_id);
    ASSERT_TRUE(last.ok());
    EXPECT_EQ(last.value()->model_name, "deep_model");

    // Should have 50 versions total
    auto list = chain_->list_versions("deep_model");
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 50u);
}

// Test 16: 100 versions performance < 1 second
TEST_F(ModelVersionChainTest, Performance100Versions) {
    auto start = std::chrono::steady_clock::now();

    std::string parent_id;
    for (int i = 0; i < 100; ++i) {
        auto result =
            chain_->create_version("perf_model", parent_id, blank_meta());
        ASSERT_TRUE(result.ok()) << result.error().message;
        parent_id = result.value().version_id;
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          end - start)
                          .count();

    EXPECT_LT(elapsed_ms, 1000)
        << "100 versions took " << elapsed_ms << "ms";

    auto list = chain_->list_versions("perf_model");
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 100u);
}

// ===== Boundary Tests =====

// Test 17: Large custom metadata (1MB) stored
TEST_F(ModelVersionChainTest, LargeCustomMetadataStored) {
    std::string large_value(1024 * 1024, 'x');  // 1MB string

    synthgen::storage::version::ModelVersion meta;
    meta.custom_metadata["big_data"] = large_value;

    auto created = chain_->create_version("model_a", "", meta);
    ASSERT_TRUE(created.ok());

    auto retrieved = chain_->get_version(created.value().version_id);
    ASSERT_TRUE(retrieved.ok());
    EXPECT_EQ(retrieved.value()->custom_metadata.at("big_data").size(),
              large_value.size());
}

// Test 18: Explain returns info
TEST_F(ModelVersionChainTest, ExplainReturnsInfo) {
    auto info = chain_->explain();
    EXPECT_EQ(info.version, "v3");
    EXPECT_EQ(info.path, "ModelVersionChain");
    EXPECT_FALSE(info.supported_statements.empty());
}

// Test 19: Parent from different model rejected
TEST_F(ModelVersionChainTest, ParentFromDifferentModelRejected) {
    auto v1 = chain_->create_version("model_a", "", blank_meta());
    ASSERT_TRUE(v1.ok());

    auto result = chain_->create_version("model_b", v1.value().version_id,
                                          blank_meta());
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code,
              synthgen::ErrorCode::kInvalidArgument);
}

// Test 20: Version ID starts with "mv_"
TEST_F(ModelVersionChainTest, VersionIdFormat) {
    auto result = chain_->create_version("model_a", "", blank_meta());
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().version_id.substr(0, 3), "mv_");
}
