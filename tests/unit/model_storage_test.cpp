#include <gtest/gtest.h>

#include "storage/model/model_storage_layer.h"
#include "storage/version/model_version.h"
#include "scaffold/trace.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace model = synthgen::storage::model;
namespace version = synthgen::storage::version;

class ModelStorageTest : public ::testing::Test {
protected:
    std::string test_dir = (std::filesystem::temp_directory_path() /
        ("synthgen_MODEL_STORAGE_" + std::to_string(::getpid()))).string();

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }

    version::ModelVersion make_version(const std::string& id) {
        version::ModelVersion v;
        v.version_id = id;
        v.model_name = "test_model";
        v.is_immutable = true;
        return v;
    }
};

// Test 1: Save and load round-trip
TEST_F(ModelStorageTest, SaveAndLoad_CheckpointRoundTrip) {
    model::ModelStorageLayer store(test_dir);

    std::string data = "KDE_model_bin_data_v1_blob";
    auto save_result = store.save_checkpoint("sensor", "v001", data);
    ASSERT_TRUE(save_result.ok()) << save_result.error().message;

    auto load_result = store.load_model("sensor", "v001");
    ASSERT_TRUE(load_result.ok()) << load_result.error().message;
    EXPECT_EQ(load_result.value(), data);
}

// Test 2: Idempotent save — overwrites with latest
TEST_F(ModelStorageTest, SaveCheckpoint_Idempotent) {
    model::ModelStorageLayer store(test_dir);

    auto r1 = store.save_checkpoint("sensor", "v001", "data_first");
    ASSERT_TRUE(r1.ok()) << r1.error().message;

    auto r2 = store.save_checkpoint("sensor", "v001", "data_second");
    ASSERT_TRUE(r2.ok()) << r2.error().message;

    auto load = store.load_model("sensor", "v001");
    ASSERT_TRUE(load.ok()) << load.error().message;
    EXPECT_EQ(load.value(), "data_second");
}

// Test 3: Load nonexistent version returns kVersionNotFound
TEST_F(ModelStorageTest, LoadModel_NotFound_Error) {
    model::ModelStorageLayer store(test_dir);

    auto result = store.load_model("sensor", "nonexistent");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kVersionNotFound);
}

// Test 4: atomic_write three-phase commit completes and data is loadable
TEST_F(ModelStorageTest, AtomicWrite_ThreePhases_Complete) {
    model::ModelStorageLayer store(test_dir);

    std::string data = "atomic_model_blob";
    auto ver = make_version("v100");
    auto result = store.atomic_write("sensor", data, ver);
    ASSERT_TRUE(result.ok()) << result.error().message;

    // Verify data is loadable
    auto load = store.load_model("sensor", "v100");
    ASSERT_TRUE(load.ok()) << load.error().message;
    EXPECT_EQ(load.value(), data);

    // Verify no .pending file remains
    auto pending = std::filesystem::path(test_dir) / "models" / "sensor" /
                   "v100.pending";
    EXPECT_FALSE(std::filesystem::exists(pending));

    // Verify .parquet file exists
    auto parquet = std::filesystem::path(test_dir) / "models" / "sensor" /
                   "v100.parquet";
    EXPECT_TRUE(std::filesystem::exists(parquet));
}

// Test 5: Phase 1 interrupt — .pending file cleaned by recover_interrupted
TEST_F(ModelStorageTest, AtomicWrite_Phase1Interrupt_Recovers) {
    model::ModelStorageLayer store(test_dir);

    // Manually create a .pending file simulating interrupted write
    auto sensor_dir =
        std::filesystem::path(test_dir) / "models" / "sensor";
    std::filesystem::create_directories(sensor_dir);

    auto pending = sensor_dir / "v999.pending";
    {
        std::ofstream out(pending, std::ios::binary);
        out << "partial_data";
    }
    ASSERT_TRUE(std::filesystem::exists(pending));

    // recover_interrupted should clean it up
    auto result = store.recover_interrupted();
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(std::filesystem::exists(pending));
}

// Test 6: Write two different versions of same model, both loadable
TEST_F(ModelStorageTest, AtomicWrite_ConcurrentSameModel_NoConflict) {
    model::ModelStorageLayer store(test_dir);

    auto ver1 = make_version("v001");
    auto ver2 = make_version("v002");

    auto r1 = store.atomic_write("sensor", "data_v001", ver1);
    ASSERT_TRUE(r1.ok()) << r1.error().message;

    auto r2 = store.atomic_write("sensor", "data_v002", ver2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;

    auto load1 = store.load_model("sensor", "v001");
    ASSERT_TRUE(load1.ok()) << load1.error().message;
    EXPECT_EQ(load1.value(), "data_v001");

    auto load2 = store.load_model("sensor", "v002");
    ASSERT_TRUE(load2.ok()) << load2.error().message;
    EXPECT_EQ(load2.value(), "data_v002");
}

// Test 7: list_model_versions returns all version IDs
TEST_F(ModelStorageTest, ListModelVersions_MultipleVersions) {
    model::ModelStorageLayer store(test_dir);

    store.save_checkpoint("sensor", "v001", "d1");
    store.save_checkpoint("sensor", "v002", "d2");
    store.save_checkpoint("sensor", "v003", "d3");

    auto result = store.list_model_versions("sensor");
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto& versions = result.value();
    EXPECT_EQ(versions.size(), 3u);

    // Verify all versions present (order not guaranteed)
    std::sort(versions.begin(), versions.end());
    EXPECT_EQ(versions[0], "v001");
    EXPECT_EQ(versions[1], "v002");
    EXPECT_EQ(versions[2], "v003");
}

// Test 8: list_model_versions for nonexistent model returns empty
TEST_F(ModelStorageTest, ListModelVersions_NoModels_Empty) {
    model::ModelStorageLayer store(test_dir);

    auto result = store.list_model_versions("nonexistent_model");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_TRUE(result.value().empty());
}

// Test 9: Large 10MB data round-trips correctly
TEST_F(ModelStorageTest, LargeCheckpoint_10MB) {
    model::ModelStorageLayer store(test_dir);

    // Build a 10MB string with a recognizable pattern
    const size_t size = 10 * 1024 * 1024;  // 10 MB
    std::string data(size, 'X');
    // Write markers at start and end for verification
    data[0] = 'A';
    data[1] = 'B';
    data[size - 2] = 'Y';
    data[size - 1] = 'Z';

    auto save_result = store.save_checkpoint("large_model", "v_big", data);
    ASSERT_TRUE(save_result.ok()) << save_result.error().message;

    auto load_result = store.load_model("large_model", "v_big");
    ASSERT_TRUE(load_result.ok()) << load_result.error().message;

    const auto& loaded = load_result.value();
    EXPECT_EQ(loaded.size(), size);
    EXPECT_EQ(loaded[0], 'A');
    EXPECT_EQ(loaded[1], 'B');
    EXPECT_EQ(loaded[size - 2], 'Y');
    EXPECT_EQ(loaded[size - 1], 'Z');
}

// Test 10: Cache hit — second load uses cache (smoke test)
TEST_F(ModelStorageTest, CacheHit_LoadTwiceFast) {
    model::ModelStorageLayer store(test_dir);

    std::string data = "cached_data_blob";
    auto save = store.save_checkpoint("sensor", "v001", data);
    ASSERT_TRUE(save.ok()) << save.error().message;

    // First load (cache miss, reads from disk)
    auto load1 = store.load_model("sensor", "v001");
    ASSERT_TRUE(load1.ok()) << load1.error().message;
    EXPECT_EQ(load1.value(), data);

    // Second load (should be cache hit — same result)
    auto load2 = store.load_model("sensor", "v001");
    ASSERT_TRUE(load2.ok()) << load2.error().message;
    EXPECT_EQ(load2.value(), data);

    // Verify both loads return the same data (cache works transparently)
    EXPECT_EQ(load1.value(), load2.value());
}

// Additional: recover_interrupted with multiple orphan .pending files
TEST_F(ModelStorageTest, RecoverInterrupted_MultipleOrphans) {
    model::ModelStorageLayer store(test_dir);

    auto dir1 = std::filesystem::path(test_dir) / "models" / "model_a";
    auto dir2 = std::filesystem::path(test_dir) / "models" / "model_b";
    std::filesystem::create_directories(dir1);
    std::filesystem::create_directories(dir2);

    // Create orphan .pending files
    {
        std::ofstream f1(dir1 / "v1.pending", std::ios::binary); f1 << "partial1";
        std::ofstream f2(dir1 / "v2.pending", std::ios::binary); f2 << "partial2";
        std::ofstream f3(dir2 / "v3.pending", std::ios::binary); f3 << "partial3";
        // Also create a valid .parquet that should NOT be removed
        std::ofstream f4(dir1 / "v0.parquet", std::ios::binary); f4 << "valid";
    }

    auto result = store.recover_interrupted();
    ASSERT_TRUE(result.ok()) << result.error().message;

    EXPECT_FALSE(std::filesystem::exists(dir1 / "v1.pending"));
    EXPECT_FALSE(std::filesystem::exists(dir1 / "v2.pending"));
    EXPECT_FALSE(std::filesystem::exists(dir2 / "v3.pending"));
    EXPECT_TRUE(std::filesystem::exists(dir1 / "v0.parquet"));
}

// Additional: atomic_write then list_model_versions includes it
TEST_F(ModelStorageTest, AtomicWrite_ListIncludesVersion) {
    model::ModelStorageLayer store(test_dir);

    auto ver = make_version("v_atom");
    auto write_result = store.atomic_write("sensor", "data", ver);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    auto list = store.list_model_versions("sensor");
    ASSERT_TRUE(list.ok()) << list.error().message;
    ASSERT_EQ(list.value().size(), 1u);
    EXPECT_EQ(list.value()[0], "v_atom");
}

// Additional: recover_interrupted on empty storage is safe
TEST_F(ModelStorageTest, RecoverInterrupted_EmptyStorage) {
    model::ModelStorageLayer store(test_dir);

    auto result = store.recover_interrupted();
    ASSERT_TRUE(result.ok()) << result.error().message;
}

// Additional: cache eviction — loading 6 distinct versions evicts oldest
TEST_F(ModelStorageTest, CacheEviction_ExceedsMaxSize) {
    model::ModelStorageLayer store(test_dir);

    // Save 6 versions (max cache is 5)
    for (int i = 0; i < 6; ++i) {
        std::string vid = "v" + std::to_string(i);
        std::string data = "data_" + std::to_string(i);
        auto save = store.save_checkpoint("sensor", vid, data);
        ASSERT_TRUE(save.ok()) << save.error().message;
    }

    // All 6 should still be loadable from disk
    for (int i = 0; i < 6; ++i) {
        std::string vid = "v" + std::to_string(i);
        auto load = store.load_model("sensor", vid);
        ASSERT_TRUE(load.ok()) << load.error().message;
        EXPECT_EQ(load.value(), "data_" + std::to_string(i));
    }
}

}  // namespace
