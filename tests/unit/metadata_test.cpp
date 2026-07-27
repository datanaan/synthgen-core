#include <gtest/gtest.h>

#include "storage/metadata.h"
#include "storage/storage_error.h"

#include <filesystem>
#include <string>

namespace {

std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / "synthgen_meta_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

// Test 1: Create and get table
TEST(MetadataTest, CreateAndGetTable) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    auto create_result = mgr.create_table("sensor_log", "{\"columns\":[...]}");
    ASSERT_TRUE(create_result.ok()) << create_result.error().message;

    auto get_result = mgr.get_table("sensor_log");
    ASSERT_TRUE(get_result.ok()) << get_result.error().message;
    EXPECT_EQ(get_result.value()->table_id, "sensor_log");
    EXPECT_EQ(get_result.value()->schema_json, "{\"columns\":[...]}");
    EXPECT_FALSE(get_result.value()->schema_hash.empty());
    EXPECT_GT(get_result.value()->created_at, 0);
}

// Test 2: Duplicate create returns error
TEST(MetadataTest, DuplicateCreateReturnsError) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    mgr.create_table("test_table", "{}");
    auto result = mgr.create_table("test_table", "{}");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableAlreadyExists);
}

// Test 3: Add version
TEST(MetadataTest, AddVersion) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);
    mgr.create_table("test_table", "{}");

    synthgen::storage::VersionMeta v1;
    v1.version_id = "v_1";
    v1.table_id = "test_table";
    v1.created_at = 1000;
    v1.row_count = 500;
    v1.schema_hash = "abc123";

    auto result = mgr.add_version("test_table", v1);
    ASSERT_TRUE(result.ok());

    auto table = mgr.get_table("test_table");
    ASSERT_TRUE(table.ok());
    EXPECT_EQ(table.value()->versions.size(), 1u);
    EXPECT_EQ(table.value()->versions[0].version_id, "v_1");
    EXPECT_EQ(table.value()->versions[0].row_count, 500);
}

// Test 4: Add snapshot
TEST(MetadataTest, AddSnapshot) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);
    mgr.create_table("test_table", "{}");

    synthgen::storage::SnapshotRef s1;
    s1.snapshot_id = "snap_001";
    s1.row_count = 1000;
    s1.created_at = 2000;

    auto result = mgr.add_snapshot("test_table", s1);
    ASSERT_TRUE(result.ok());

    auto table = mgr.get_table("test_table");
    ASSERT_TRUE(table.ok());
    EXPECT_EQ(table.value()->snapshots.size(), 1u);
    EXPECT_EQ(table.value()->snapshots[0].snapshot_id, "snap_001");
    EXPECT_EQ(table.value()->snapshots[0].row_count, 1000);
}

// Test 5: Flush and reload consistency
TEST(MetadataTest, FlushAndReloadConsistency) {
    auto dir = make_temp_dir();
    {
        synthgen::storage::MetadataManager mgr(dir);
        mgr.create_table("t1", "{\"type\":\"sensor\"}");

        synthgen::storage::VersionMeta v;
        v.version_id = "v_1";
        v.table_id = "t1";
        v.created_at = 1000;
        v.row_count = 42;
        v.schema_hash = "hash123";
        mgr.add_version("t1", v);

        synthgen::storage::SnapshotRef s;
        s.snapshot_id = "snap_1";
        s.row_count = 42;
        s.created_at = 2000;
        mgr.add_snapshot("t1", s);

        auto flush_result = mgr.flush();
        ASSERT_TRUE(flush_result.ok()) << flush_result.error().message;
    }

    // Reload in a new manager instance
    {
        synthgen::storage::MetadataManager mgr2(dir);
        auto reload_result = mgr2.reload();
        ASSERT_TRUE(reload_result.ok()) << reload_result.error().message;

        auto table = mgr2.get_table("t1");
        ASSERT_TRUE(table.ok()) << table.error().message;
        EXPECT_EQ(table.value()->table_id, "t1");
        EXPECT_EQ(table.value()->schema_json, "{\"type\":\"sensor\"}");
        EXPECT_EQ(table.value()->versions.size(), 1u);
        EXPECT_EQ(table.value()->versions[0].version_id, "v_1");
        EXPECT_EQ(table.value()->versions[0].row_count, 42);
        EXPECT_EQ(table.value()->versions[0].schema_hash, "hash123");
        EXPECT_EQ(table.value()->snapshots.size(), 1u);
        EXPECT_EQ(table.value()->snapshots[0].snapshot_id, "snap_1");
    }
}

// Test 6: Non-existent table returns error
TEST(MetadataTest, NonExistentTableReturnsError) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    auto result = mgr.get_table("no_such_table");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound);
}

// Test 7: Add version to non-existent table
TEST(MetadataTest, AddVersionToNonExistentTable) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    synthgen::storage::VersionMeta v;
    v.version_id = "v_1";
    auto result = mgr.add_version("no_table", v);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kTableNotFound);
}

// Test 8: Multiple tables
TEST(MetadataTest, MultipleTables) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    mgr.create_table("t1", "{}");
    mgr.create_table("t2", "{}");
    mgr.create_table("t3", "{}");

    auto t1 = mgr.get_table("t1");
    auto t2 = mgr.get_table("t2");
    auto t3 = mgr.get_table("t3");
    ASSERT_TRUE(t1.ok());
    ASSERT_TRUE(t2.ok());
    ASSERT_TRUE(t3.ok());
    EXPECT_EQ(t1.value()->table_id, "t1");
    EXPECT_EQ(t2.value()->table_id, "t2");
    EXPECT_EQ(t3.value()->table_id, "t3");
}

// Test 9: Next part number increments
TEST(MetadataTest, NextPartNumber) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);

    EXPECT_EQ(mgr.next_part_number(), 1);
    EXPECT_EQ(mgr.next_part_number(), 2);
    EXPECT_EQ(mgr.next_part_number(), 3);
}

// Test 10: Reload with empty directory
TEST(MetadataTest, ReloadEmptyDirectory) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr(dir);
    auto result = mgr.reload();
    EXPECT_TRUE(result.ok());
}

// Test 11: Schema hash is deterministic
TEST(MetadataTest, SchemaHashDeterministic) {
    auto dir = make_temp_dir();
    synthgen::storage::MetadataManager mgr1(dir);
    mgr1.create_table("t1", "{\"cols\":[\"a\",\"b\"]}");
    auto h1 = mgr1.get_table("t1").value()->schema_hash;

    auto dir2 = make_temp_dir();
    synthgen::storage::MetadataManager mgr2(dir2);
    mgr2.create_table("t1", "{\"cols\":[\"a\",\"b\"]}");
    auto h2 = mgr2.get_table("t1").value()->schema_hash;

    EXPECT_EQ(h1, h2);
}
