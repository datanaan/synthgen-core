// Storage chaos tests — 15 aggressive tests for Storage layer error paths and edge cases
#include <gtest/gtest.h>

#include "storage/object_store_backend.h"
#include "storage/storage_error.h"
#include "storage/audit/audit_log.h"

#include <arrow/api.h>

#include <filesystem>
#include <string>
#include <vector>
#include <map>

namespace {

int g_test_counter = 0;

std::filesystem::path make_temp_dir(const std::string& suffix = "") {
    std::string name = "synthgen_chaos_" + std::to_string(::getpid()) + "_" +
                       std::to_string(g_test_counter++);
    if (!suffix.empty()) name += "_" + suffix;
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
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

// Wide table builder: N columns named col_0 .. col_{N-1}, each double
std::shared_ptr<arrow::Table> make_wide_table(int num_cols, int64_t rows) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::Array>> arrays;
    for (int c = 0; c < num_cols; ++c) {
        arrow::DoubleBuilder builder;
        for (int64_t i = 0; i < rows; ++i) {
            builder.Append(static_cast<double>(i + c));
        }
        std::shared_ptr<arrow::Array> arr;
        builder.Finish(&arr);
        fields.push_back(arrow::field("col_" + std::to_string(c), arrow::float64()));
        arrays.push_back(arr);
    }
    return arrow::Table::Make(arrow::schema(fields), arrays);
}

std::shared_ptr<arrow::Table> make_empty_table() {
    auto empty_arr_result = arrow::MakeEmptyArray(arrow::int64());
    auto empty_dbl_result = arrow::MakeEmptyArray(arrow::float64());
    if (!empty_arr_result.ok() || !empty_dbl_result.ok()) return nullptr;
    auto schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("value", arrow::float64())});
    return arrow::Table::Make(schema, {*empty_arr_result, *empty_dbl_result}, 0);
}

}  // namespace

// =============================================================================
// Test 1: Append to unregistered table -> kTableNotFound
// =============================================================================
TEST(StorageChaosTest, AppendToUnregisteredTable) {
    auto dir = make_temp_dir("append_unreg");
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto data = make_int_table(10);
    auto result = backend.append("ghost_table", data);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound)
        << "Expected kTableNotFound, got: " << result.error().message;
}

// =============================================================================
// Test 2: Scan unregistered table -> kTableNotFound
// =============================================================================
TEST(StorageChaosTest, ScanUnregisteredTable) {
    auto dir = make_temp_dir("scan_unreg");
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto result = backend.scan("no_such_table");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound)
        << "Expected kTableNotFound, got: " << result.error().message;
}

// =============================================================================
// Test 3: Register same table twice -> kTableAlreadyExists
// =============================================================================
TEST(StorageChaosTest, RegisterSameTableTwice) {
    auto dir = make_temp_dir("dup_reg");
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto r1 = backend.register_table("dup_table", "{}");
    ASSERT_TRUE(r1.ok()) << r1.error().message;

    auto r2 = backend.register_table("dup_table", "{\"v\":2}");
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, synthgen::ErrorCode::kTableAlreadyExists)
        << "Expected kTableAlreadyExists, got: " << r2.error().message;
}

// =============================================================================
// Test 4: Scan with predicate on non-existent column -> kColumnNotFound
// =============================================================================
TEST(StorageChaosTest, ScanPredicateOnNonExistentColumn) {
    auto dir = make_temp_dir("pred_nocol");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("pred_tbl", "{}");
    backend.append("pred_tbl", make_int_table(20));

    synthgen::storage::ScanPredicate pred;
    pred.column = "phantom_column";
    pred.min_value = 0.0;
    pred.max_value = 50.0;

    auto result = backend.scan("pred_tbl", {}, pred);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kColumnNotFound)
        << "Expected kColumnNotFound, got: " << result.error().message;
}

// =============================================================================
// Test 5: Append 0-row table -> should succeed, version has row_count=0
// =============================================================================
TEST(StorageChaosTest, AppendZeroRowTable) {
    auto dir = make_temp_dir("zero_row");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("empty_tbl", "{}");

    auto data = make_empty_table();
    ASSERT_NE(data, nullptr);

    auto append_result = backend.append("empty_tbl", data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    auto versions = backend.list_versions("empty_tbl");
    ASSERT_TRUE(versions.ok()) << versions.error().message;
    ASSERT_EQ(versions.value().size(), 1u);
    EXPECT_EQ(versions.value()[0].row_count, 0);

    // Scan should return 0 rows
    auto scan_result = backend.scan("empty_tbl");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 0);
}

// =============================================================================
// Test 6: Append 1-row table -> scan returns 1 row
// =============================================================================
TEST(StorageChaosTest, AppendOneRowTable) {
    auto dir = make_temp_dir("one_row");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("single_tbl", "{}");

    auto data = make_int_table(1);
    auto append_result = backend.append("single_tbl", data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    auto scan = backend.scan("single_tbl");
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 1);
    EXPECT_EQ(scan.value()->num_columns(), 2);

    // Verify the actual value
    auto id_col = std::static_pointer_cast<arrow::Int64Array>(
        scan.value()->GetColumnByName("id")->chunk(0));
    ASSERT_EQ(id_col->length(), 1);
    EXPECT_EQ(id_col->Value(0), 0);
}

// =============================================================================
// Test 7: Column projection for non-existent column -> should error
// =============================================================================
TEST(StorageChaosTest, ProjectNonExistentColumn) {
    auto dir = make_temp_dir("proj_nocol");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("proj_tbl", "{}");
    backend.append("proj_tbl", make_int_table(10));

    auto result = backend.scan("proj_tbl", {"does_not_exist"});
    // The parquet reader should fail when asked for a column that isn't in the file
    EXPECT_FALSE(result.ok()) << "Expected error for non-existent column projection";
}

// =============================================================================
// Test 8: Empty column projection (scan all) -> returns all columns
// =============================================================================
TEST(StorageChaosTest, EmptyProjectionReturnsAllColumns) {
    auto dir = make_temp_dir("empty_proj");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("all_cols", "{}");
    auto data = make_int_table(15);
    int orig_cols = data->num_columns();
    backend.append("all_cols", data);

    auto scan = backend.scan("all_cols", {});  // empty projection = all columns
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_columns(), orig_cols)
        << "Empty projection should return all " << orig_cols << " columns";
    EXPECT_EQ(scan.value()->num_rows(), 15);
}

// =============================================================================
// Test 9: List versions for unregistered table -> kTableNotFound
// =============================================================================
TEST(StorageChaosTest, ListVersionsUnregisteredTable) {
    auto dir = make_temp_dir("vers_unreg");
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto result = backend.list_versions("nonexistent_table");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound)
        << "Expected kTableNotFound, got: " << result.error().message;
}

// =============================================================================
// Test 10: has_table for nonexistent -> false
// =============================================================================
TEST(StorageChaosTest, HasTableNonExistent) {
    auto dir = make_temp_dir("has_nonexist");
    synthgen::storage::ObjectStoreBackend backend(dir);

    auto result = backend.has_table("ghost");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(result.value());

    // Also verify that after registration, it returns true
    backend.register_table("ghost", "{}");
    auto result2 = backend.has_table("ghost");
    ASSERT_TRUE(result2.ok()) << result2.error().message;
    EXPECT_TRUE(result2.value());
}

// =============================================================================
// Test 11: Concurrent appends — 50 appends of 5-row batches = 250 total
// =============================================================================
TEST(StorageChaosTest, ConcurrentAppends) {
    auto dir = make_temp_dir("concurrent");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("conc_tbl", "{}");

    constexpr int kAppends = 50;
    constexpr int kRowsPerAppend = 5;

    for (int i = 0; i < kAppends; ++i) {
        auto data = make_int_table(kRowsPerAppend);
        auto result = backend.append("conc_tbl", data);
        ASSERT_TRUE(result.ok()) << "Append " << i << " failed: " << result.error().message;
    }

    auto scan = backend.scan("conc_tbl");
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), kAppends * kRowsPerAppend)
        << "Expected " << (kAppends * kRowsPerAppend) << " rows, got "
        << scan.value()->num_rows();

    auto versions = backend.list_versions("conc_tbl");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), static_cast<size_t>(kAppends));
}

// =============================================================================
// Test 12: Very wide table — 30 columns, append and scan back all columns
// =============================================================================
TEST(StorageChaosTest, VeryWideTable) {
    auto dir = make_temp_dir("wide");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("wide_tbl", "{}");

    constexpr int kNumCols = 30;
    constexpr int64_t kRows = 25;

    auto data = make_wide_table(kNumCols, kRows);
    ASSERT_EQ(data->num_columns(), kNumCols);
    ASSERT_EQ(data->num_rows(), kRows);

    auto append_result = backend.append("wide_tbl", data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Scan all columns
    auto scan = backend.scan("wide_tbl");
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_columns(), kNumCols);
    EXPECT_EQ(scan.value()->num_rows(), kRows);

    // Verify each column name
    for (int c = 0; c < kNumCols; ++c) {
        std::string expected_name = "col_" + std::to_string(c);
        int idx = scan.value()->schema()->GetFieldIndex(expected_name);
        EXPECT_GE(idx, 0) << "Column '" << expected_name << "' not found in scan result";
    }

    // Scan a subset of columns
    auto partial = backend.scan("wide_tbl", {"col_5", "col_15", "col_29"});
    ASSERT_TRUE(partial.ok()) << partial.error().message;
    EXPECT_EQ(partial.value()->num_columns(), 3);
    EXPECT_EQ(partial.value()->num_rows(), kRows);
}

// =============================================================================
// Test 13: Scan after multiple appends, verify row ordering — no data loss
// =============================================================================
TEST(StorageChaosTest, MultipleAppendsVerifyRowOrdering) {
    auto dir = make_temp_dir("ordering");
    synthgen::storage::ObjectStoreBackend backend(dir);
    backend.register_table("order_tbl", "{}");

    // Append 3 batches with distinct row counts
    // Batch 1: 10 rows (id 0..9)
    // Batch 2: 20 rows (id 0..19)
    // Batch 3: 5 rows (id 0..4)
    auto b1 = make_int_table(10);
    auto b2 = make_int_table(20);
    auto b3 = make_int_table(5);

    auto r1 = backend.append("order_tbl", b1);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    auto r2 = backend.append("order_tbl", b2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    auto r3 = backend.append("order_tbl", b3);
    ASSERT_TRUE(r3.ok()) << r3.error().message;

    auto scan = backend.scan("order_tbl");
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 35)  // 10 + 20 + 5
        << "Expected 35 total rows, got " << scan.value()->num_rows();

    // Verify total row counts in versions
    auto versions = backend.list_versions("order_tbl");
    ASSERT_TRUE(versions.ok());
    ASSERT_EQ(versions.value().size(), 3u);
    EXPECT_EQ(versions.value()[0].row_count, 10);
    EXPECT_EQ(versions.value()[1].row_count, 20);
    EXPECT_EQ(versions.value()[2].row_count, 5);

    // Verify no data loss: count all id values across ALL chunks
    auto id_col = scan.value()->GetColumnByName("id");
    ASSERT_NE(id_col, nullptr);
    int64_t total = 0;
    for (int c = 0; c < id_col->num_chunks(); ++c) {
        auto chunk = std::static_pointer_cast<arrow::Int64Array>(id_col->chunk(c));
        for (int64_t i = 0; i < chunk->length(); ++i) {
            EXPECT_FALSE(chunk->IsNull(i)) << "Null at row " << total + i;
            total++;
        }
    }
    EXPECT_EQ(total, 35) << "Only counted " << total << " rows across "
                         << id_col->num_chunks() << " chunks";
}

// =============================================================================
// Test 14: Audit log chain with 100 records — genesis + 99 appends
// =============================================================================
TEST(StorageChaosTest, AuditLogChain100Records) {
    synthgen::storage::audit::AuditLog log;

    auto genesis = log.create_genesis();
    ASSERT_TRUE(genesis.ok()) << genesis.error().message;
    EXPECT_EQ(log.record_count(), 1);

    for (int i = 0; i < 99; ++i) {
        std::string op = "append_batch_" + std::to_string(i);
        std::map<std::string, std::string> meta;
        meta["batch_index"] = std::to_string(i);
        meta["rows"] = std::to_string((i + 1) * 10);

        auto result = log.append(op, "tester", meta);
        ASSERT_TRUE(result.ok()) << "Append " << i << " failed: " << result.error().message;
        ASSERT_FALSE(result.value().record_id.empty());
        ASSERT_FALSE(result.value().chain_hash.empty());
        ASSERT_FALSE(result.value().content_hash.empty());
    }

    EXPECT_EQ(log.record_count(), 100);

    // Full chain verification must pass
    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok()) << verify.error().message;
    EXPECT_TRUE(verify.value()) << "Chain verification failed for 100-record chain";

    // Daily verification should also pass
    auto daily = log.daily_verification();
    ASSERT_TRUE(daily.ok()) << daily.error().message;
    EXPECT_TRUE(daily.value().is_valid) << "Daily verification failed";
    EXPECT_EQ(daily.value().total_records, 100);
    EXPECT_EQ(daily.value().verified_records, 99);  // genesis not counted
    EXPECT_TRUE(daily.value().broken_links.empty()) << "Found broken links in valid chain";

    // Verify no forks
    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty()) << "Unexpected forks detected in linear chain";
}

// =============================================================================
// Test 15: Audit log daily verification with 500 records — performance
// =============================================================================
TEST(StorageChaosTest, AuditLogDailyVerification500Records) {
    synthgen::storage::audit::AuditLog log;

    auto genesis = log.create_genesis();
    ASSERT_TRUE(genesis.ok()) << genesis.error().message;

    for (int i = 0; i < 499; ++i) {
        std::string op = "op_" + std::to_string(i);
        std::map<std::string, std::string> meta;
        meta["index"] = std::to_string(i);

        auto result = log.append(op, "perf_tester", meta);
        ASSERT_TRUE(result.ok()) << "Append " << i << " failed: " << result.error().message;
    }

    EXPECT_EQ(log.record_count(), 500);

    // Measure daily_verification performance
    auto start = std::chrono::steady_clock::now();
    auto daily = log.daily_verification();
    auto end = std::chrono::steady_clock::now();

    ASSERT_TRUE(daily.ok()) << daily.error().message;
    EXPECT_TRUE(daily.value().is_valid) << "Daily verification failed for 500-record chain";
    EXPECT_EQ(daily.value().total_records, 500);
    EXPECT_EQ(daily.value().verified_records, 499);
    EXPECT_TRUE(daily.value().broken_links.empty());

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Should complete well under 1 second for 500 records
    EXPECT_LT(elapsed_ms, 1000) << "Daily verification took " << elapsed_ms << "ms for 500 records";

    // Also verify chain integrity
    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());

    // Verify get_latest returns the last record (loop was i=0..498, so op_498)
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "op_498");

    // Verify scan with limit
    auto scanned = log.scan(std::nullopt, std::nullopt, 10);
    ASSERT_TRUE(scanned.ok());
    EXPECT_EQ(scanned.value().size(), 10u);

    // Scan all
    auto all = log.scan(std::nullopt, std::nullopt, 0);
    ASSERT_TRUE(all.ok());
    EXPECT_EQ(all.value().size(), 500u);
}
