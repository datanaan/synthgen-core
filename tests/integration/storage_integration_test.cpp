#include <gtest/gtest.h>

#include "storage/object_store_backend.h"
#include "storage/storage_error.h"

#include <arrow/api.h>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path make_temp_dir(const std::string& suffix = "") {
    static int counter = 0;
    std::string name = "synthgen_integ_" + std::to_string(::getpid()) + "_" +
                       std::to_string(counter++);
    if (!suffix.empty()) name += "_" + suffix;
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::shared_ptr<arrow::Table> make_sensor_table(int64_t rows) {
    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder speed_builder;
    arrow::DoubleBuilder temp_builder;
    for (int64_t i = 0; i < rows; ++i) {
        ts_builder.Append(i * 1000);
        speed_builder.Append(static_cast<double>(i) * 2.5);
        temp_builder.Append(20.0 + static_cast<double>(i) * 0.5);
    }
    std::shared_ptr<arrow::Array> ts_array, speed_array, temp_array;
    ts_builder.Finish(&ts_array);
    speed_builder.Finish(&speed_array);
    temp_builder.Finish(&temp_array);
    auto schema = arrow::schema(
        {arrow::field("timestamp", arrow::int64()),
         arrow::field("wind_speed", arrow::float64()),
         arrow::field("temperature", arrow::float64())});
    return arrow::Table::Make(schema, {ts_array, speed_array, temp_array});
}

std::shared_ptr<arrow::Table> make_int_table(int64_t rows) {
    arrow::Int64Builder id_builder;
    arrow::DoubleBuilder val_builder;
    for (int64_t i = 0; i < rows; ++i) {
        id_builder.Append(i);
        val_builder.Append(static_cast<double>(i % 100));
    }
    std::shared_ptr<arrow::Array> id_array, val_array;
    id_builder.Finish(&id_array);
    val_builder.Finish(&val_array);
    auto schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("value", arrow::float64())});
    return arrow::Table::Make(schema, {id_array, val_array});
}

}  // namespace

// Test 1: Full flow — register, append, scan, list_versions
TEST(StorageIntegrationTest, FullFlow) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto reg = backend.register_table("sensor_log", "{\"type\":\"sensor_log\"}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    auto data = make_sensor_table(100);
    auto append_result = backend.append("sensor_log", data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;
    EXPECT_FALSE(append_result.value().empty());

    auto scan_result = backend.scan("sensor_log");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 100);
    EXPECT_EQ(scan_result.value()->num_columns(), 3);

    auto versions = backend.list_versions("sensor_log");
    ASSERT_TRUE(versions.ok()) << versions.error().message;
    EXPECT_EQ(versions.value().size(), 1u);
    EXPECT_EQ(versions.value()[0].row_count, 100);
}

// Test 2: Multiple appends (10 parts)
TEST(StorageIntegrationTest, MultipleAppends) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    backend.register_table("multi", "{}");

    for (int i = 0; i < 10; ++i) {
        auto data = make_int_table(50);
        auto result = backend.append("multi", data);
        ASSERT_TRUE(result.ok()) << "append " << i << " failed: " << result.error().message;
    }

    auto scan = backend.scan("multi");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 500);

    auto versions = backend.list_versions("multi");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 10u);
}

// Test 3: Column projection scan
TEST(StorageIntegrationTest, ColumnProjectionScan) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    backend.register_table("proj_test", "{}");
    auto data = make_sensor_table(20);
    backend.append("proj_test", data);

    auto scan = backend.scan("proj_test", {"temperature"});
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_columns(), 1);
    EXPECT_EQ(scan.value()->num_rows(), 20);
    EXPECT_EQ(scan.value()->schema()->field(0)->name(), "temperature");
}

// Test 4: Non-existent table scan returns kTableNotFound
TEST(StorageIntegrationTest, ScanNonExistentTable) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto result = backend.scan("no_table");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound);
}

// Test 5: Duplicate registration returns kTableAlreadyExists
TEST(StorageIntegrationTest, DuplicateRegistration) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    backend.register_table("dup", "{}");
    auto result = backend.register_table("dup", "{}");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableAlreadyExists);
}

// Test 6: Large data (10000 rows)
TEST(StorageIntegrationTest, LargeData) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    backend.register_table("large", "{}");
    auto data = make_int_table(10000);
    auto append_result = backend.append("large", data);
    ASSERT_TRUE(append_result.ok());

    auto scan = backend.scan("large");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 10000);
}

// Test 7: Restart simulation (new backend instance, same dir)
TEST(StorageIntegrationTest, RestartSimulation) {
    auto dir = make_temp_dir();
    std::string table_id = "persistent";

    {
        synthgen::storage::ObjectStoreBackend backend(dir);
        backend.register_table(table_id, "{\"type\":\"sensor\"}");
        auto data = make_sensor_table(50);
        backend.append(table_id, data);

        auto scan = backend.scan(table_id);
        ASSERT_TRUE(scan.ok());
        EXPECT_EQ(scan.value()->num_rows(), 50);
    }

    // Simulate restart — create a new backend pointing to the same dir
    {
        synthgen::storage::ObjectStoreBackend backend2(dir);

        // Table should still exist
        auto has = backend2.has_table(table_id);
        ASSERT_TRUE(has.ok());
        EXPECT_TRUE(has.value());

        // Can scan data
        auto scan = backend2.scan(table_id);
        ASSERT_TRUE(scan.ok()) << scan.error().message;
        EXPECT_EQ(scan.value()->num_rows(), 50);

        // Can append more data
        auto data = make_sensor_table(30);
        auto append_result = backend2.append(table_id, data);
        ASSERT_TRUE(append_result.ok()) << append_result.error().message;

        auto scan2 = backend2.scan(table_id);
        ASSERT_TRUE(scan2.ok());
        EXPECT_EQ(scan2.value()->num_rows(), 80);
    }
}

// Test 8: Unregistered append returns error
TEST(StorageIntegrationTest, UnregisteredAppend) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto data = make_int_table(10);
    auto result = backend.append("no_table", data);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound);
}

// Test 9: 0-row append
TEST(StorageIntegrationTest, ZeroRowAppend) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("empty", "{}");

    // Create an empty table with a real empty array
    arrow::Int64Builder builder;
    std::shared_ptr<arrow::Array> empty_array;
    builder.Finish(&empty_array);
    auto empty_typed = arrow::MakeEmptyArray(arrow::int64());
    ASSERT_TRUE(empty_typed.ok());
    auto schema = arrow::schema({arrow::field("x", arrow::int64())});
    auto empty = arrow::Table::Make(schema, {*empty_typed}, 0);
    auto result = backend.append("empty", empty);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto versions = backend.list_versions("empty");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value()[0].row_count, 0);
}

// Test 10: 1-row append
TEST(StorageIntegrationTest, OneRowAppend) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("single", "{}");

    auto data = make_int_table(1);
    auto result = backend.append("single", data);
    ASSERT_TRUE(result.ok());

    auto scan = backend.scan("single");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 1);
}

// Test 11: has_table works correctly
TEST(StorageIntegrationTest, HasTable) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto has_before = backend.has_table("test");
    ASSERT_TRUE(has_before.ok());
    EXPECT_FALSE(has_before.value());

    backend.register_table("test", "{}");

    auto has_after = backend.has_table("test");
    ASSERT_TRUE(has_after.ok());
    EXPECT_TRUE(has_after.value());
}

// Test 12: Scan with predicate
TEST(StorageIntegrationTest, ScanWithPredicate) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("pred_test", "{}");

    auto data = make_int_table(100);  // values 0..99
    backend.append("pred_test", data);

    synthgen::storage::ScanPredicate pred;
    pred.column = "value";
    pred.min_value = 10.0;
    pred.max_value = 20.0;

    auto scan = backend.scan("pred_test", {}, pred);
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    // Values 10..20 inclusive = 11 rows
    EXPECT_EQ(scan.value()->num_rows(), 11);
}

// Test 13: Scan with predicate on non-existent column returns error
TEST(StorageIntegrationTest, ScanPredicateNonExistentColumn) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("pred_err", "{}");

    auto data = make_int_table(10);
    backend.append("pred_err", data);

    synthgen::storage::ScanPredicate pred;
    pred.column = "nonexistent";
    pred.min_value = 0.0;
    pred.max_value = 100.0;

    auto scan = backend.scan("pred_err", {}, pred);
    EXPECT_FALSE(scan.ok());
    EXPECT_EQ(scan.error().code, synthgen::ErrorCode::kColumnNotFound);
}

// Test 14: list_versions for non-existent table
TEST(StorageIntegrationTest, ListVersionsNonExistent) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto result = backend.list_versions("no_table");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound);
}

// Test 15: Column projection + multiple parts
TEST(StorageIntegrationTest, ColumnProjectionMultipleParts) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("multi_proj", "{}");

    for (int i = 0; i < 3; ++i) {
        auto data = make_sensor_table(10);
        backend.append("multi_proj", data);
    }

    auto scan = backend.scan("multi_proj", {"wind_speed"});
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 30);
    EXPECT_EQ(scan.value()->num_columns(), 1);
    EXPECT_EQ(scan.value()->schema()->field(0)->name(), "wind_speed");
}

// Test 16: Scan empty table (registered but no data appended)
TEST(StorageIntegrationTest, ScanEmptyTable) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("empty_scan", "{}");

    auto scan = backend.scan("empty_scan");
    // Should succeed but return an empty table
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 0);
}

// Test 17: Multiple tables independent
TEST(StorageIntegrationTest, MultipleTablesIndependent) {
    auto dir = make_temp_dir();
    synthgen::storage::ObjectStoreBackend backend(dir);

    backend.register_table("t1", "{}");
    backend.register_table("t2", "{}");

    auto d1 = make_int_table(10);
    auto d2 = make_int_table(20);

    backend.append("t1", d1);
    backend.append("t2", d2);

    auto s1 = backend.scan("t1");
    auto s2 = backend.scan("t2");
    ASSERT_TRUE(s1.ok());
    ASSERT_TRUE(s2.ok());
    EXPECT_EQ(s1.value()->num_rows(), 10);
    EXPECT_EQ(s2.value()->num_rows(), 20);

    auto v1 = backend.list_versions("t1");
    auto v2 = backend.list_versions("t2");
    ASSERT_TRUE(v1.ok());
    ASSERT_TRUE(v2.ok());
    EXPECT_EQ(v1.value().size(), 1u);
    EXPECT_EQ(v2.value().size(), 1u);
}
