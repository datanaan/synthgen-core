#include <gtest/gtest.h>

#include "storage/timetravel/time_travel_engine.h"
#include "storage/version/model_version_chain.h"
#include "storage/model/model_storage_layer.h"
#include "storage/metadata.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <vector>

using namespace synthgen;
using namespace synthgen::storage;
using namespace synthgen::storage::version;
using namespace synthgen::storage::model;
using namespace synthgen::storage::timetravel;

class TimeTravelTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_TIMETRAVEL_" + std::to_string(::getpid()))).string();
    MetadataManager meta{test_dir};
    ModelVersionChain chain{meta};
    ModelStorageLayer storage{test_dir + "/storage"};
    TimeTravelEngine engine{chain, storage};

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        std::filesystem::create_directories(test_dir + "/storage");
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    // Helper: create a version in chain and save its checkpoint
    ModelVersion create_and_save(
        const std::string& model_name,
        const std::string& parent_id,
        const std::string& data) {
        ModelVersion v;
        v.model_name = model_name;
        v.training_data_range = "2025-01-01..2025-06-01";
        v.fidelity_score = 0.95;
        v.training_rows = 1000;

        auto result = chain.create_version(model_name, parent_id, v);
        EXPECT_TRUE(result.ok()) << result.error().message;

        auto save_result = storage.save_checkpoint(
            model_name, result.value().version_id, data);
        EXPECT_TRUE(save_result.ok()) << save_result.error().message;

        return result.value();
    }

    // Helper: create a version in chain and write checkpoint directly to
    // disk (bypasses ModelStorageLayer's internal cache so that file
    // deletion is correctly reflected on subsequent load_model calls).
    ModelVersion create_and_write_file(
        const std::string& model_name,
        const std::string& parent_id,
        const std::string& data) {
        ModelVersion v;
        v.model_name = model_name;
        v.training_data_range = "2025-01-01..2025-06-01";
        v.fidelity_score = 0.95;
        v.training_rows = 1000;

        auto result = chain.create_version(model_name, parent_id, v);
        EXPECT_TRUE(result.ok()) << result.error().message;

        // Write directly to the expected path
        auto dir = std::filesystem::path(test_dir + "/storage/models/" + model_name);
        std::filesystem::create_directories(dir);
        auto path = dir / (result.value().version_id + ".parquet");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();

        return result.value();
    }
};

// 1. QueryExistingVersion_ReturnsData
TEST_F(TimeTravelTest, QueryExistingVersion_ReturnsData) {
    auto v = create_and_save("sensor_x", "", "model_data_v1");

    auto result = engine.query_as_of("sensor_x", v.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data, "model_data_v1");
}

// 2. QueryExistingVersion_NotDegraded
TEST_F(TimeTravelTest, QueryExistingVersion_NotDegraded) {
    auto v = create_and_save("sensor_x", "", "model_data_v1");

    auto result = engine.query_as_of("sensor_x", v.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value().was_degraded);
    EXPECT_FALSE(result.value().bias_report.has_value());
}

// 3. QueryNonexistentVersion_ReturnsError (no fallback available)
TEST_F(TimeTravelTest, QueryNonexistentVersion_ReturnsError) {
    auto result = engine.query_as_of("sensor_x", "nonexistent_version");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNoAvailableVersion);
}

// 4. QueryWithDegradation_ReturnsNearest
TEST_F(TimeTravelTest, QueryWithDegradation_ReturnsNearest) {
    auto v1 = create_and_write_file("sensor_y", "", "data_v1");
    auto v2 = create_and_write_file("sensor_y", v1.version_id, "data_v2");
    auto v3 = create_and_write_file("sensor_y", v2.version_id, "data_v3");

    // Delete v1's checkpoint file to simulate compaction
    auto path = std::filesystem::path(test_dir + "/storage/models/sensor_y/" +
                                      v1.version_id + ".parquet");
    ASSERT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);

    // Query for v1 → should degrade to the nearest available (most recent)
    auto result = engine.query_as_of("sensor_y", v1.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().was_degraded);
    // Most recent available is v3 (list_versions returns sorted by created_at desc)
    EXPECT_EQ(result.value().data, "data_v3");
}

// 5. Degradation_HasBiasReport
TEST_F(TimeTravelTest, Degradation_HasBiasReport) {
    auto v1 = create_and_write_file("sensor_z", "", "data_v1");
    auto v2 = create_and_write_file("sensor_z", v1.version_id, "data_v2");

    // Delete v1's checkpoint
    auto path = std::filesystem::path(test_dir + "/storage/models/sensor_z/" +
                                      v1.version_id + ".parquet");
    std::filesystem::remove(path);

    auto result = engine.query_as_of("sensor_z", v1.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().was_degraded);
    ASSERT_TRUE(result.value().bias_report.has_value());

    const auto& report = result.value().bias_report.value();
    EXPECT_EQ(report.requested_version, v1.version_id);
    EXPECT_EQ(report.returned_version, v2.version_id);
    EXPECT_EQ(report.reason, "compacted");
    EXPECT_TRUE(report.version_mismatch);
}

// 6. MultipleVersions_CorrectData
TEST_F(TimeTravelTest, MultipleVersions_CorrectData) {
    auto v1 = create_and_save("sensor_w", "", "alpha");
    auto v2 = create_and_save("sensor_w", v1.version_id, "beta");
    auto v3 = create_and_save("sensor_w", v2.version_id, "gamma");

    // Query each version → each should return its own data
    auto r1 = engine.query_as_of("sensor_w", v1.version_id);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    EXPECT_EQ(r1.value().data, "alpha");
    EXPECT_FALSE(r1.value().was_degraded);

    auto r2 = engine.query_as_of("sensor_w", v2.version_id);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_EQ(r2.value().data, "beta");
    EXPECT_FALSE(r2.value().was_degraded);

    auto r3 = engine.query_as_of("sensor_w", v3.version_id);
    ASSERT_TRUE(r3.ok()) << r3.error().message;
    EXPECT_EQ(r3.value().data, "gamma");
    EXPECT_FALSE(r3.value().was_degraded);
}

// 7. EmptyModel_NotFound
TEST_F(TimeTravelTest, EmptyModel_NotFound) {
    auto result = engine.query_as_of("nonexistent_model", "any_version");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNoAvailableVersion);
}

// 8. BiasReportFieldsCorrect
TEST_F(TimeTravelTest, BiasReportFieldsCorrect) {
    auto v1 = create_and_write_file("sensor_b", "", "data_1");
    auto v2 = create_and_write_file("sensor_b", v1.version_id, "data_2");
    auto v3 = create_and_write_file("sensor_b", v2.version_id, "data_3");

    // Delete v1 and v2 checkpoints, only v3 remains
    auto dir = std::filesystem::path(test_dir + "/storage/models/sensor_b");
    std::filesystem::remove(dir / (v1.version_id + ".parquet"));
    std::filesystem::remove(dir / (v2.version_id + ".parquet"));

    // Query for v1
    auto result = engine.query_as_of("sensor_b", v1.version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& report = result.value().bias_report.value();
    EXPECT_EQ(report.requested_version, v1.version_id);
    EXPECT_EQ(report.returned_version, v3.version_id);
    EXPECT_TRUE(report.version_mismatch);
    EXPECT_EQ(report.reason, "compacted");
    EXPECT_FALSE(report.returned_version.empty());
}

// 9. EmptyModelName_ReturnsInvalidArgument
TEST_F(TimeTravelTest, EmptyModelName_ReturnsInvalidArgument) {
    auto result = engine.query_as_of("", "some_version");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// 10. EmptyVersionId_ReturnsInvalidArgument
TEST_F(TimeTravelTest, EmptyVersionId_ReturnsInvalidArgument) {
    auto result = engine.query_as_of("sensor_x", "");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// 11. VersionInChainButNotInStorage_DegradesGracefully
TEST_F(TimeTravelTest, VersionInChainButNotInStorage_DegradesGracefully) {
    // Create a version in the chain but do NOT save a checkpoint
    ModelVersion meta;
    meta.model_name = "sensor_d";
    meta.fidelity_score = 0.88;
    auto v1 = chain.create_version("sensor_d", "", meta);
    ASSERT_TRUE(v1.ok());

    // Save a different version that IS in storage
    auto v2 = create_and_save("sensor_d", v1.value().version_id, "fallback_data");

    // Query for v1 (in chain but not in storage) → should degrade to v2
    auto result = engine.query_as_of("sensor_d", v1.value().version_id);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().was_degraded);
    EXPECT_EQ(result.value().data, "fallback_data");
}

// 12. AllVersionsCompacted_NoAvailableVersion
TEST_F(TimeTravelTest, AllVersionsCompacted_NoAvailableVersion) {
    auto v1 = create_and_write_file("sensor_e", "", "data_1");
    auto v2 = create_and_write_file("sensor_e", v1.version_id, "data_2");

    // Delete both checkpoint files
    auto dir = std::filesystem::path(test_dir + "/storage/models/sensor_e");
    std::filesystem::remove(dir / (v1.version_id + ".parquet"));
    std::filesystem::remove(dir / (v2.version_id + ".parquet"));

    // Query for v1 → no available versions at all
    auto result = engine.query_as_of("sensor_e", v1.version_id);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNoAvailableVersion);
}
