#include <gtest/gtest.h>
#include "engine/alignment/continuous_alignment_engine.h"
#include "engine/alignment/test_model_protocol.h"
#include "storage/metadata.h"
#include "storage/version/model_version_chain.h"
#include "storage/model/model_storage_layer.h"

#include <filesystem>
#include <random>
#include <unistd.h>
#include <vector>

using namespace synthgen;
using namespace synthgen::engine::alignment;
using namespace synthgen::storage;
using namespace synthgen::storage::version;
using namespace synthgen::storage::model;

namespace {

std::vector<double> generate_normal(double mean, double stddev, int n, uint64_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(mean, stddev);
    std::vector<double> result(n);
    for (int i = 0; i < n; ++i) result[i] = dist(rng);
    return result;
}

}  // namespace

class ContinuousAlignmentTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_ALIGNMENT_" + std::to_string(::getpid()))).string();
    MetadataManager meta{test_dir};
    ModelVersionChain chain{meta};
    ModelStorageLayer storage{test_dir + "/storage"};
    ContinuousAlignmentEngine engine{chain, storage};

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        std::filesystem::create_directories(test_dir + "/storage");
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

// ---------------------------------------------------------------------------
// Test 1: First alignment with empty current_data creates a version
// ---------------------------------------------------------------------------
TEST_F(ContinuousAlignmentTest, FirstAlignment_CreatesVersion) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_version_id = "";
    req.current_data = {};
    req.new_data = generate_normal(5.0, 1.0, 100, 42);

    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().new_version.version_id.empty());
    EXPECT_EQ(result.value().new_version.model_name, "sensor_model");
}

// Test 2: First alignment should not detect drift
TEST_F(ContinuousAlignmentTest, FirstAlignment_NoDrift) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = {};
    req.new_data = generate_normal(5.0, 1.0, 100, 42);

    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_DOUBLE_EQ(result.value().drift_score, 0.0);
}

// Test 3: Drift is detected when distributions shift
TEST_F(ContinuousAlignmentTest, DriftDetected_WhenDataShifts) {
    // First alignment
    AlignmentRequest req1;
    req1.model_name = "sensor_model";
    req1.current_data = {};
    req1.new_data = generate_normal(0.0, 1.0, 200, 42);
    auto r1 = engine.update_model(req1);
    ASSERT_TRUE(r1.ok());

    // Second alignment with shifted data
    AlignmentRequest req2;
    req2.model_name = "sensor_model";
    req2.current_version_id = r1.value().new_version.version_id;
    req2.current_data = generate_normal(0.0, 1.0, 200, 42);
    req2.new_data = generate_normal(5.0, 1.0, 200, 123);
    auto r2 = engine.update_model(req2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_TRUE(r2.value().drift_detected);
    EXPECT_GT(r2.value().drift_score, 0.1);
}

// Test 4: No drift when data is from the same distribution
TEST_F(ContinuousAlignmentTest, NoDrift_SameData) {
    AlignmentRequest req1;
    req1.model_name = "sensor_model";
    req1.current_data = {};
    req1.new_data = generate_normal(5.0, 1.0, 200, 42);
    auto r1 = engine.update_model(req1);
    ASSERT_TRUE(r1.ok());

    AlignmentRequest req2;
    req2.model_name = "sensor_model";
    req2.current_version_id = r1.value().new_version.version_id;
    req2.current_data = generate_normal(5.0, 1.0, 200, 42);
    req2.new_data = generate_normal(5.0, 1.0, 200, 99);
    auto r2 = engine.update_model(req2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_FALSE(r2.value().drift_detected);
}

// Test 5: Moderate drift shows converging status
TEST_F(ContinuousAlignmentTest, CompensationStatus_Converging) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = generate_normal(0.0, 1.0, 200, 42);
    req.new_data = generate_normal(0.5, 1.0, 200, 99);  // mild shift

    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    // First alignment with data but only one score — not enough for converged/diverging
    EXPECT_EQ(result.value().compensation_status, "converging");
}

// Test 6: Three consecutive low drift scores → converged
TEST_F(ContinuousAlignmentTest, CompensationStatus_Converged) {
    for (int i = 0; i < 3; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_data = generate_normal(5.0, 1.0, 200, 42);
        req.new_data = generate_normal(5.0, 1.0, 200, 42 + i);  // same distribution
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;

        if (i == 2) {
            EXPECT_EQ(result.value().compensation_status, "converged");
        }
    }
}

// Test 7: Five consecutive high drift scores → diverging
TEST_F(ContinuousAlignmentTest, CompensationStatus_Diverging) {
    for (int i = 0; i < 5; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_data = generate_normal(0.0, 1.0, 200, 42);
        req.new_data = generate_normal(10.0 + i, 1.0, 200, 100 + i);  // big shift
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;

        if (i == 4) {
            EXPECT_EQ(result.value().compensation_status, "diverging");
        }
    }
}

// Test 8: Parent version ID is set correctly in the new version
TEST_F(ContinuousAlignmentTest, NewVersion_ParentSet) {
    AlignmentRequest req1;
    req1.model_name = "sensor_model";
    req1.current_data = {};
    req1.new_data = generate_normal(5.0, 1.0, 100, 42);
    auto r1 = engine.update_model(req1);
    ASSERT_TRUE(r1.ok());

    auto parent_id = r1.value().new_version.version_id;

    AlignmentRequest req2;
    req2.model_name = "sensor_model";
    req2.current_version_id = parent_id;
    req2.current_data = generate_normal(5.0, 1.0, 100, 42);
    req2.new_data = generate_normal(5.0, 1.0, 100, 99);
    auto r2 = engine.update_model(req2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_EQ(r2.value().new_version.parent_version_id, parent_id);
}

// Test 9: Drift score is stored in version metadata
TEST_F(ContinuousAlignmentTest, NewVersion_MetadataStored) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = generate_normal(0.0, 1.0, 200, 42);
    req.new_data = generate_normal(5.0, 1.0, 200, 123);
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& meta = result.value().new_version.custom_metadata;
    EXPECT_TRUE(meta.count("drift_score") > 0);
    EXPECT_TRUE(meta.count("drift_detected") > 0);
    EXPECT_TRUE(meta.count("data_size") > 0);
    EXPECT_TRUE(meta.count("data_mean") > 0);
    EXPECT_TRUE(meta.count("data_min") > 0);
    EXPECT_TRUE(meta.count("data_max") > 0);
}

// Test 10: Checkpoint is saved and loadable
TEST_F(ContinuousAlignmentTest, CheckpointSaved) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = {};
    req.new_data = {1.0, 2.0, 3.0, 4.0, 5.0};
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto load_result = storage.load_model(
        "sensor_model", result.value().new_version.version_id);
    ASSERT_TRUE(load_result.ok()) << load_result.error().message;
    EXPECT_FALSE(load_result.value().empty());
    EXPECT_TRUE(load_result.value().find("drift_score") != std::string::npos);
}

// Test 11: Empty model name returns error
TEST_F(ContinuousAlignmentTest, EmptyModelName_Error) {
    AlignmentRequest req;
    req.model_name = "";
    req.new_data = {1.0, 2.0, 3.0};
    auto result = engine.update_model(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// Test 12: Empty new_data returns error
TEST_F(ContinuousAlignmentTest, EmptyNewData_Error) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.new_data = {};
    auto result = engine.update_model(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kEmptyTrainingData);
}

// Test 13: Nonexistent parent version returns error
TEST_F(ContinuousAlignmentTest, NonexistentParent_ReturnsError) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_version_id = "mv_nonexistent_12345";
    req.current_data = generate_normal(0.0, 1.0, 100, 42);
    req.new_data = generate_normal(5.0, 1.0, 100, 99);
    auto result = engine.update_model(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kParentNotFound);
}

// Test 14: drift_check="none" skips detection
TEST_F(ContinuousAlignmentTest, DriftCheckNone_SkipsDetection) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = generate_normal(0.0, 1.0, 200, 42);
    req.new_data = generate_normal(100.0, 1.0, 200, 123);  // massive shift
    req.drift_check = "none";
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().drift_detected);
    EXPECT_DOUBLE_EQ(result.value().drift_score, 0.0);
}

// Test 15: Multiple alignments with decreasing drift → converged
TEST_F(ContinuousAlignmentTest, MultipleAlignments_TrackConvergence) {
    // Run 3 alignments with the same distribution (low drift)
    for (int i = 0; i < 3; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_data = generate_normal(5.0, 1.0, 200, 42);
        req.new_data = generate_normal(5.0, 1.0, 200, 42 + i * 10);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
        if (i == 2) {
            EXPECT_EQ(result.value().compensation_status, "converged");
        }
    }
}

// Test 16: Multiple alignments with increasing drift → diverging
TEST_F(ContinuousAlignmentTest, MultipleAlignments_TrackDivergence) {
    for (int i = 0; i < 5; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_data = generate_normal(0.0, 1.0, 200, 42);
        // Progressively larger shift each iteration
        req.new_data = generate_normal(5.0 + i * 5.0, 1.0, 200, 100 + i);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
        if (i == 4) {
            EXPECT_EQ(result.value().compensation_status, "diverging");
        }
    }
}

// Test 17: Deadline is set and reflected in result
TEST_F(ContinuousAlignmentTest, CompensationDeadline_Set) {
    int64_t deadline = 1000000;
    engine.set_compensation_deadline("sensor_model", deadline);

    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = {};
    req.new_data = {1.0, 2.0, 3.0};
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().compensation_deadline, deadline);
}

// ---------------------------------------------------------------------------
// Additional tests beyond the minimum 17
// ---------------------------------------------------------------------------

// Test 18: drift_check="auto" resolves to "ks"
TEST_F(ContinuousAlignmentTest, DriftCheckAuto_ResolvesToKs) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.drift_check = "auto";
    req.current_data = generate_normal(0.0, 1.0, 200, 42);
    req.new_data = generate_normal(5.0, 1.0, 200, 123);
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    // Should detect drift since "auto" resolves to "ks"
    EXPECT_TRUE(result.value().drift_detected);
}

// Test 19: Different models track convergence independently
TEST_F(ContinuousAlignmentTest, DifferentModels_IndependentTracking) {
    // Model A: converging
    for (int i = 0; i < 2; ++i) {
        AlignmentRequest req;
        req.model_name = "model_a";
        req.current_data = generate_normal(5.0, 1.0, 200, 42);
        req.new_data = generate_normal(5.0, 1.0, 200, 42 + i);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok());
    }

    // Model B: diverging
    for (int i = 0; i < 2; ++i) {
        AlignmentRequest req;
        req.model_name = "model_b";
        req.current_data = generate_normal(0.0, 1.0, 200, 42);
        req.new_data = generate_normal(10.0 + i * 5.0, 1.0, 200, 100 + i);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok());
    }

    // Now push model_a to converged
    AlignmentRequest req_a;
    req_a.model_name = "model_a";
    req_a.current_data = generate_normal(5.0, 1.0, 200, 42);
    req_a.new_data = generate_normal(5.0, 1.0, 200, 77);
    auto r_a = engine.update_model(req_a);
    ASSERT_TRUE(r_a.ok());
    EXPECT_EQ(r_a.value().compensation_status, "converged");

    // model_b is still not converged (only 3 scores, not all below threshold)
    AlignmentRequest req_b;
    req_b.model_name = "model_b";
    req_b.current_data = generate_normal(0.0, 1.0, 200, 42);
    req_b.new_data = generate_normal(20.0, 1.0, 200, 55);
    auto r_b = engine.update_model(req_b);
    ASSERT_TRUE(r_b.ok());
    EXPECT_NE(r_b.value().compensation_status, "converged");
}

// Test 20: Version chain lists multiple versions for a model
TEST_F(ContinuousAlignmentTest, VersionChain_MultipleVersions) {
    std::string prev_id;
    for (int i = 0; i < 3; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_version_id = prev_id;
        req.current_data = generate_normal(5.0, 1.0, 100, 42);
        req.new_data = generate_normal(5.0, 1.0, 100, 42 + i * 10);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok());
        prev_id = result.value().new_version.version_id;
    }

    auto versions = chain.list_versions("sensor_model");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 3u);
}

// Test 21: First version has empty parent_version_id
TEST_F(ContinuousAlignmentTest, FirstVersion_EmptyParent) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = {};
    req.new_data = {1.0, 2.0, 3.0};
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().new_version.parent_version_id.empty());
    EXPECT_TRUE(result.value().new_version.is_first_version());
}

// Test 22: Created_by is set to "alignment_engine"
TEST_F(ContinuousAlignmentTest, CreatedBy_SetCorrectly) {
    AlignmentRequest req;
    req.model_name = "sensor_model";
    req.current_data = {};
    req.new_data = {1.0, 2.0, 3.0};
    auto result = engine.update_model(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().new_version.created_by, "alignment_engine");
}

// Test 23: Timeout degraded when past deadline and not converged
TEST_F(ContinuousAlignmentTest, TimeoutDegraded_WhenPastDeadline) {
    // Set deadline in the past (0 = already expired)
    engine.set_compensation_deadline("sensor_model", 0);

    // Push diverging scores
    for (int i = 0; i < 5; ++i) {
        AlignmentRequest req;
        req.model_name = "sensor_model";
        req.current_data = generate_normal(0.0, 1.0, 200, 42);
        req.new_data = generate_normal(10.0, 1.0, 200, 100 + i);
        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
        if (i == 4) {
            EXPECT_EQ(result.value().compensation_status, "timeout_degraded");
        }
    }
}

// Test 24: TestModelProtocol interface can be implemented (compiles)
TEST(TestModelProtocolTest, InterfaceCompiles) {
    struct MockModel : TestModelProtocol {
        std::string model_id() const override { return "mock_v1"; }
        std::string model_type() const override { return "kde"; }
        Result<double> query_density(const std::vector<double>&) const override {
            return 0.5;
        }
        Result<std::vector<double>> query_boundary(const std::string&) const override {
            return std::vector<double>{0.0, 1.0};
        }
    };

    MockModel m;
    EXPECT_EQ(m.model_id(), "mock_v1");
    EXPECT_EQ(m.model_type(), "kde");

    auto density = m.query_density({1.0, 2.0});
    ASSERT_TRUE(density.ok());
    EXPECT_DOUBLE_EQ(density.value(), 0.5);

    auto boundary = m.query_boundary("x > 0");
    ASSERT_TRUE(boundary.ok());
    EXPECT_EQ(boundary.value().size(), 2u);
}
