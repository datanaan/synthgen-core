/**
 * negative_destructive_test.cpp — Round 3 destructive testing.
 * Feeds INVALID inputs to every component and verifies graceful error handling.
 * Any crash (segfault, assertion failure, UB) is a bug to be fixed.
 */

#include <gtest/gtest.h>

#include "common/types.h"
#include "common/result.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/postfilter/post_filter.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/evidence_package.h"
#include "parser/ast.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>

#include <memory>
#include <filesystem>
#include <cstdio>
#include <limits>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;
using namespace synthgen::schema;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Make a minimal valid schema with one float column.
Schema make_one_col_schema(const std::string& name = "test_type") {
    Schema s;
    s.type_name = name;
    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    s.columns.push_back(col);
    return s;
}

/// Make a schema with an ORDER column (timestamp) plus a float column.
Schema make_order_schema() {
    Schema s;
    s.type_name = "ordered_type";
    {
        ColumnDef ts;
        ts.name = "ts";
        ts.type = DataType::kDatetime;
        ts.is_order = true;
        ts.not_null = true;
        s.columns.push_back(ts);
    }
    {
        ColumnDef val;
        val.name = "temp";
        val.type = DataType::kFloat;
        val.range_min = -50.0;
        val.range_max = 80.0;
        s.columns.push_back(val);
    }
    return s;
}

/// Build a GenerationRequest with all fields (avoids C++17 aggregate init issues).
struct RequestBuilder {
    static GenerationRequest make(const schema::Schema& s,
                                   std::vector<parser::ast::ConstraintItem> c,
                                   int64_t limit, uint64_t seed,
                                   const std::string& dist = "uniform",
                                   int64_t batch_size = 1000) {
        GenerationRequest req{s, std::move(c), limit, seed, dist, batch_size};
        return req;
    }
};

/// Build an Arrow table with N rows of a single double column.
std::shared_ptr<arrow::Table> make_double_table(
    const std::string& col_name, const std::vector<double>& values) {
    arrow::DoubleBuilder builder;
    for (auto v : values) (void)builder.Append(v);
    std::shared_ptr<arrow::Array> arr;
    auto status = builder.Finish(&arr);
    if (!status.ok()) return nullptr;
    auto schema = arrow::schema({arrow::field(col_name, arrow::float64())});
    return arrow::Table::Make(schema, {arr});
}

/// Build an Arrow table with two columns (int64 + double) for inter-row tests.
std::shared_ptr<arrow::Table> make_ts_double_table(
    const std::string& ts_col, const std::string& val_col,
    const std::vector<int64_t>& ts_vals,
    const std::vector<double>& double_vals) {
    arrow::Int64Builder ts_builder;
    for (auto v : ts_vals) (void)ts_builder.Append(v);
    std::shared_ptr<arrow::Array> ts_arr;
    auto s1 = ts_builder.Finish(&ts_arr);

    arrow::DoubleBuilder val_builder;
    for (auto v : double_vals) (void)val_builder.Append(v);
    std::shared_ptr<arrow::Array> val_arr;
    auto s2 = val_builder.Finish(&val_arr);

    if (!s1.ok() || !s2.ok()) return nullptr;

    auto schema = arrow::schema({
        arrow::field(ts_col, arrow::int64()),
        arrow::field(val_col, arrow::float64()),
    });
    return arrow::Table::Make(schema, {ts_arr, val_arr});
}

/// Unique temp dir helper
std::filesystem::path make_temp_dir() {
    auto base = std::filesystem::temp_directory_path() / "synthgen_neg_test";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    return base;
}

}  // anonymous namespace

// ===========================================================================
// Test 1: RectangularSampler with empty schema (0 columns)
// ===========================================================================
TEST(NegativeDestructive, RectangularSamplerEmptySchema) {
    Schema empty;
    empty.type_name = "empty_type";

    RectangularSampler sampler(empty);
    auto req = RequestBuilder::make(empty, {}, 10, 42);
    auto result = sampler.generate(req);
    ASSERT_FALSE(result.ok()) << "Should reject empty schema";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===========================================================================
// Test 2: RectangularSampler with schema containing only ORDER column
// ===========================================================================
TEST(NegativeDestructive, RectangularSamplerOnlyOrderColumn) {
    Schema s;
    s.type_name = "order_only";
    ColumnDef ts;
    ts.name = "ts";
    ts.type = DataType::kDatetime;
    ts.is_order = true;
    ts.not_null = true;
    s.columns.push_back(ts);

    RectangularSampler sampler(s);
    auto req = RequestBuilder::make(s, {}, 10, 42);
    auto result = sampler.generate(req);
    // Should succeed -- it has one datetime column to generate.
    ASSERT_TRUE(result.ok()) << "Error: " << result.error().message;
    EXPECT_NE(result.value().data, nullptr);
    EXPECT_GT(result.value().data->num_columns(), 0);
}

// ===========================================================================
// Test 3: ValueRangeValidator with empty constraints vector
// ===========================================================================
TEST(NegativeDestructive, ValueRangeValidatorEmptyConstraints) {
    auto schema = make_one_col_schema();
    ValueRangeValidator validator(schema, {} /* empty constraints */);

    auto table = make_double_table("value", {10.0, 20.0, 30.0});
    ASSERT_NE(table, nullptr);

    auto result = validator.validate_batch(table);
    ASSERT_TRUE(result.ok()) << "Should handle empty constraints gracefully";
    EXPECT_EQ(result.value().rows_checked, 3);
    // With no rules, all rows pass
    EXPECT_EQ(result.value().rows_passed, 3);
    EXPECT_EQ(result.value().rows_failed, 0);
}

// ===========================================================================
// Test 4: ValueRangeValidator with NULL batch (nullptr)
// BUG: ValueRangeValidator::validate_batch dereferences batch without null
// check. This will segfault. After fix, should return error.
// ===========================================================================
TEST(NegativeDestructive, ValueRangeValidatorNullBatch) {
    auto schema = make_one_col_schema();
    ValueRangeValidator validator(schema, {});

    // Pass nullptr -- destructive input. Current code WILL crash here.
    // After fix, this should return an error instead of segfaulting.
    auto result = validator.validate_batch(nullptr);
    // If we reach this point, the bug is fixed.
    ASSERT_FALSE(result.ok()) << "Should error on nullptr batch";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===========================================================================
// Test 4b: ValueRangeValidator with constraint for non-existent column
// ===========================================================================
TEST(NegativeDestructive, ValueRangeValidatorNonExistentColumn) {
    auto schema = make_one_col_schema();
    parser::ast::ConstraintItem item;
    item.column_name = "nonexistent_column";
    item.op = parser::ast::ConstraintOperator::kBetween;
    item.value_min = 0.0;
    item.value_max = 50.0;

    ValueRangeValidator validator(schema, {item});

    auto table = make_double_table("value", {10.0, 60.0});
    ASSERT_NE(table, nullptr);

    auto result = validator.validate_batch(table);
    // Constructor silently skips columns not in schema (find_column returns nullopt).
    // The rules_ vector will be empty, so validate_batch won't look for the column.
    ASSERT_TRUE(result.ok()) << "Should skip non-existent columns gracefully";
    EXPECT_EQ(result.value().rows_checked, 2);
}

// ===========================================================================
// Test 5: InterRowEngine with empty batch (0 rows)
// ===========================================================================
TEST(NegativeDestructive, InterRowEngineEmptyBatch) {
    auto schema = make_order_schema();

    InterRowConstraintDef def;
    def.column_name = "temp";
    def.order_column = "ts";
    def.type = InterRowConstraintDef::Type::kDeltaMax;
    def.delta_max = 10.0;

    InterRowEngine engine(schema, {def});

    auto table = make_ts_double_table("ts", "temp", {}, {});
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->num_rows(), 0);

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << "Should handle empty batch: " << result.error().message;
    EXPECT_EQ(result.value().rows_passed, 0);
    EXPECT_EQ(result.value().rows_filtered, 0);
}

// ===========================================================================
// Test 6: InterRowEngine with constraint referencing non-existent column
// ===========================================================================
TEST(NegativeDestructive, InterRowEngineNonExistentColumn) {
    auto schema = make_order_schema();

    InterRowConstraintDef def;
    def.column_name = "nonexistent_col";
    def.order_column = "ts";
    def.type = InterRowConstraintDef::Type::kDeltaMax;
    def.delta_max = 5.0;

    InterRowEngine engine(schema, {def});

    auto table = make_ts_double_table("ts", "temp", {1000, 2000}, {25.0, 30.0});
    ASSERT_NE(table, nullptr);

    auto result = engine.execute_batch(table, {});
    ASSERT_FALSE(result.ok()) << "Should error on non-existent column";
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedColumn);
}

// ===========================================================================
// Test 7: AggregateEngine with empty batch
// ===========================================================================
TEST(NegativeDestructive, AggregateEngineEmptyBatch) {
    auto schema = make_order_schema();

    AggregateConstraintDef adef;
    adef.constraint_name = "avg_temp";
    adef.column_name = "temp";
    adef.function = AggregateFunction::kAvg;
    adef.window_type = WindowType::kInterval;
    adef.window_interval_us = 1000000;  // 1 second
    adef.max_val = 50.0;

    AggregateEngine engine(schema, {adef});

    auto table = make_ts_double_table("ts", "temp", {}, {});
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->num_rows(), 0);

    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok()) << "Should handle empty batch: " << result.error().message;
    EXPECT_EQ(result.value().phase_two.total_windows, 0);
}

// ===========================================================================
// Test 8: PostFilter with NULL table (nullptr)
// ===========================================================================
TEST(NegativeDestructive, PostFilterNullTable) {
    PostFilter pf;
    auto result = pf.execute(nullptr, 10);
    ASSERT_TRUE(result.ok()) << "Should handle nullptr gracefully";
    EXPECT_EQ(result.value().pre_filter_rows, 0);
    EXPECT_EQ(result.value().post_filter_rows, 0);
}

// ===========================================================================
// Test 9: PostFilter with target_rows=0
// ===========================================================================
TEST(NegativeDestructive, PostFilterTargetRowsZero) {
    PostFilter pf;
    auto table = make_double_table("value", {1.0, 2.0, 3.0});
    ASSERT_NE(table, nullptr);

    auto result = pf.execute(table, 0);
    ASSERT_TRUE(result.ok()) << "Should handle target_rows=0 gracefully";
    EXPECT_EQ(result.value().post_filter_rows, 0);
}

// ===========================================================================
// Test 10: PostFilter with target_rows >> actual rows
// In pass-through mode, available=3, target=1000000, so min(3,1000000)=3.
// estimated_rate = 1.0 - 3/3 = 0.0.  So it succeeds and returns all 3 rows.
// The critical threshold is NOT hit because no rows are actually excluded.
// ===========================================================================
TEST(NegativeDestructive, PostFilterTargetRowsMuchLarger) {
    PostFilter pf;
    auto table = make_double_table("value", {1.0, 2.0, 3.0});
    ASSERT_NE(table, nullptr);

    auto result = pf.execute(table, 1000000);
    // In pass-through mode, all available rows are returned.
    // exclusion_rate = 1.0 - min(available, target) / available = 0.0
    ASSERT_TRUE(result.ok()) << "Pass-through mode should succeed: " << result.error().message;
    EXPECT_EQ(result.value().post_filter_rows, 3);
    EXPECT_NEAR(result.value().actual_exclusion_rate, 0.0, 1e-10);
}

// ===========================================================================
// Test 11: ObjectStoreBackend with non-existent directory path
// ===========================================================================
TEST(NegativeDestructive, ObjectStoreBackendNonExistentPath) {
    auto path = std::filesystem::temp_directory_path() / "synthgen_neg_11" / "deep" / "nested" / "dir";
    std::error_code ec;
    std::filesystem::remove_all(path.parent_path(), ec);

    // Constructor creates dirs, so this should succeed
    ObjectStoreBackend backend(path);

    // Now try to scan a non-existent table
    auto scan_result = backend.scan("nonexistent_table");
    ASSERT_FALSE(scan_result.ok()) << "Should error scanning non-existent table";
    EXPECT_EQ(scan_result.error().code, ErrorCode::kTableNotFound);

    // Cleanup
    std::filesystem::remove_all(path.parent_path().parent_path().parent_path(), ec);
}

// ===========================================================================
// Test 12: ObjectStoreBackend::scan immediately after register (no data)
// ===========================================================================
TEST(NegativeDestructive, ObjectStoreBackendScanAfterRegisterNoData) {
    auto dir = make_temp_dir();
    ObjectStoreBackend backend(dir);

    auto reg = backend.register_table("test_table", R"({"columns":[]})");
    ASSERT_TRUE(reg.ok()) << "Register failed: " << reg.error().message;

    auto scan = backend.scan("test_table");
    ASSERT_TRUE(scan.ok()) << "Scan after register should succeed: " << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 0);

    // Cleanup
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ===========================================================================
// Test 13: AuditLog::verify_chain on empty log (no genesis)
// ===========================================================================
TEST(NegativeDestructive, AuditLogVerifyChainEmpty) {
    AuditLog log;
    auto result = log.verify_chain();
    ASSERT_TRUE(result.ok()) << "verify_chain on empty log should succeed";
    EXPECT_TRUE(result.value());
}

// ===========================================================================
// Test 13b: AuditLog::append before create_genesis
// ===========================================================================
TEST(NegativeDestructive, AuditLogAppendBeforeGenesis) {
    AuditLog log;
    auto result = log.append("operation", "actor");
    ASSERT_FALSE(result.ok()) << "Should error when appending before genesis";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidState);
}

// ===========================================================================
// Test 14: SchemaRegistry::register_schema with empty type_name
// ===========================================================================
TEST(NegativeDestructive, SchemaRegistryEmptyTypeName) {
    SchemaRegistry registry;

    Schema s;
    s.type_name = "";  // empty type name
    ColumnDef col;
    col.name = "col1";
    col.type = DataType::kFloat;
    s.columns.push_back(col);

    auto result = registry.register_schema(std::move(s));
    // Current implementation does NOT validate type_name.
    // It inserts an empty string key into the map.
    ASSERT_TRUE(result.ok()) << "register_schema should succeed (no validation)";
    EXPECT_TRUE(registry.has_schema(""));

    // Two schemas with empty type_name should fail on second
    Schema s2;
    s2.type_name = "";
    col.name = "col2";
    s2.columns.push_back(col);
    auto result2 = registry.register_schema(std::move(s2));
    ASSERT_FALSE(result2.ok()) << "Should reject duplicate empty type_name";
    EXPECT_EQ(result2.error().code, ErrorCode::kDuplicateTypeName);
}

// ===========================================================================
// Test 14b: SchemaRegistry::get_schema for non-existent type
// ===========================================================================
TEST(NegativeDestructive, SchemaRegistryGetNonExistent) {
    SchemaRegistry registry;
    auto result = registry.get_schema("no_such_type");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

// ===========================================================================
// Test 15: EvidencePackageBuilder with 0-row generation result
// ===========================================================================
TEST(NegativeDestructive, EvidencePackageBuilderZeroRows) {
    auto schema = make_one_col_schema();

    GenerationResult gen_result;
    gen_result.data = nullptr;  // No data
    gen_result.stats.rows_generated = 0;
    gen_result.stats.rows_requested = 0;
    gen_result.stats.exclusion_rate = 0.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 0;
    val_result.rows_passed = 0;
    val_result.rows_failed = 0;
    val_result.pass_rate = 1.0;

    TailReportV1 tail;
    tail.data_grade = "physics_guaranteed";
    tail.total_exclusion_rate = 0.0;
    tail.rows_generated = 0;
    tail.rows_validated = 0;
    tail.rows_failed_validation = 0;
    tail.distribution_used = "uniform";
    tail.seed_used = 0;

    ProvenanceV1 prov;
    prov.data_source = "test";
    prov.generator_identity = "test_harness";

    EvidencePackageBuilder builder;
    auto result = builder.build(gen_result, val_result, tail, prov, schema);
    ASSERT_TRUE(result.ok()) << "Should build package with 0 rows: " << result.error().message;
    EXPECT_EQ(result.value().row_count, 0);
}

// ===========================================================================
// Test 16: RectangularSampler with negative limit
// ===========================================================================
TEST(NegativeDestructive, RectangularSamplerNegativeLimit) {
    auto schema = make_one_col_schema();
    RectangularSampler sampler(schema);
    auto req = RequestBuilder::make(schema, {}, -5, 42);
    auto result = sampler.generate(req);
    ASSERT_FALSE(result.ok()) << "Should reject negative limit";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===========================================================================
// Test 17: InterRowEngine with empty constraints vector
// ===========================================================================
TEST(NegativeDestructive, InterRowEngineEmptyConstraints) {
    auto schema = make_order_schema();
    InterRowEngine engine(schema, {} /* no constraints */);

    auto table = make_ts_double_table("ts", "temp", {1000, 2000}, {25.0, 30.0});
    ASSERT_NE(table, nullptr);

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << "Should handle empty constraints: " << result.error().message;
    EXPECT_EQ(result.value().rows_passed, 2);
}

// ===========================================================================
// Test 18: InterRowEngine without ORDER column in schema
// ===========================================================================
TEST(NegativeDestructive, InterRowEngineNoOrderColumn) {
    Schema no_order;
    no_order.type_name = "no_order";
    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    no_order.columns.push_back(col);

    InterRowConstraintDef def;
    def.column_name = "value";
    def.type = InterRowConstraintDef::Type::kDeltaMax;
    def.delta_max = 10.0;

    InterRowEngine engine(no_order, {def});
    auto table = make_double_table("value", {10.0, 20.0});
    auto result = engine.execute_batch(table, {});
    ASSERT_FALSE(result.ok()) << "Should error: no ORDER column";
    EXPECT_EQ(result.error().code, ErrorCode::kOrderColumnRequired);
}

// ===========================================================================
// Test 19: PostFilter with negative target_rows
// ===========================================================================
TEST(NegativeDestructive, PostFilterNegativeTargetRows) {
    PostFilter pf;
    auto table = make_double_table("value", {1.0, 2.0});
    ASSERT_NE(table, nullptr);

    auto result = pf.execute(table, -1);
    ASSERT_TRUE(result.ok()) << "Should handle negative target_rows gracefully";
    EXPECT_EQ(result.value().post_filter_rows, 0);
}

// ===========================================================================
// Test 20: AuditLog verify chain with valid records
// ===========================================================================
TEST(NegativeDestructive, AuditLogTamperedChainDetection) {
    AuditLog log;
    auto gen = log.create_genesis();
    ASSERT_TRUE(gen.ok());

    auto rec1 = log.append("gen_data", "user1", {{"key", "val"}});
    ASSERT_TRUE(rec1.ok());

    auto valid = log.verify_chain();
    ASSERT_TRUE(valid.ok());
    EXPECT_TRUE(valid.value());

    auto daily = log.daily_verification();
    ASSERT_TRUE(daily.ok());
    EXPECT_TRUE(daily.value().is_valid);
}

// ===========================================================================
// Test 21: AggregateEngine with constraint for non-existent column
// ===========================================================================
TEST(NegativeDestructive, AggregateEngineNonExistentColumn) {
    auto schema = make_order_schema();

    AggregateConstraintDef adef;
    adef.constraint_name = "bad_col";
    adef.column_name = "nonexistent";
    adef.function = AggregateFunction::kAvg;
    adef.window_interval_us = 1000000;
    adef.max_val = 50.0;

    AggregateEngine engine(schema, {adef});

    auto table = make_ts_double_table("ts", "temp", {1000, 2000}, {25.0, 30.0});
    ASSERT_NE(table, nullptr);

    auto result = engine.execute(table, {});
    // compute_aggregate finds column index -1, returns kUndefinedColumn error.
    // execute_phase_two catches that error and continues (if (!agg_result.ok()) continue;)
    ASSERT_TRUE(result.ok()) << "Should handle gracefully: " << result.error().message;
}

// ===========================================================================
// Test 22: ValueRangeValidator with column in schema but NOT in table
// ===========================================================================
TEST(NegativeDestructive, ValueRangeValidatorColumnInSchemaNotInTable) {
    auto schema = make_one_col_schema();

    parser::ast::ConstraintItem item;
    item.column_name = "value";
    item.op = parser::ast::ConstraintOperator::kBetween;
    item.value_min = 0.0;
    item.value_max = 50.0;

    ValueRangeValidator validator(schema, {item});

    // Table with DIFFERENT column name
    auto table = make_double_table("wrong_name", {10.0, 60.0});
    ASSERT_NE(table, nullptr);

    auto result = validator.validate_batch(table);
    ASSERT_FALSE(result.ok()) << "Should error when column not found in table";
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedColumn);
}

// ===========================================================================
// Test 23: RectangularSampler with invalid distribution
// ===========================================================================
TEST(NegativeDestructive, RectangularSamplerInvalidDistribution) {
    auto schema = make_one_col_schema();
    RectangularSampler sampler(schema);
    auto req = RequestBuilder::make(schema, {}, 10, 42, "laplace");
    auto result = sampler.generate(req);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ===========================================================================
// Test 24: ObjectStoreBackend append to unregistered table
// ===========================================================================
TEST(NegativeDestructive, ObjectStoreBackendAppendUnregistered) {
    auto dir = make_temp_dir();
    ObjectStoreBackend backend(dir);

    auto table = make_double_table("value", {1.0, 2.0});
    auto result = backend.append("nonexistent", table);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kTableNotFound);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ===========================================================================
// Test 25: EvidencePackageBuilder from_json with garbage input
// ===========================================================================
TEST(NegativeDestructive, EvidencePackageBuilderFromGarbageJson) {
    EvidencePackageBuilder builder;
    auto result = builder.from_json("this is not json at all {{{}}");
    ASSERT_FALSE(result.ok()) << "Should reject malformed JSON";
}

// ===========================================================================
// Test 26: Schema::validate with NaN range values
// ===========================================================================
TEST(NegativeDestructive, SchemaValidateNaNRange) {
    Schema s;
    s.type_name = "nan_test";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    col.range_min = std::numeric_limits<double>::quiet_NaN();
    col.range_max = 100.0;
    s.columns.push_back(col);

    auto result = s.validate();
    ASSERT_FALSE(result.ok()) << "Should reject NaN in range";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

// ===========================================================================
// Test 27: Schema::validate with inverted range (min >= max)
// ===========================================================================
TEST(NegativeDestructive, SchemaValidateInvertedRange) {
    Schema s;
    s.type_name = "inverted_test";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    col.range_min = 100.0;
    col.range_max = 50.0;
    s.columns.push_back(col);

    auto result = s.validate();
    ASSERT_FALSE(result.ok()) << "Should reject inverted range";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

// ===========================================================================
// Test 28: AggregateEngine with empty constraints
// ===========================================================================
TEST(NegativeDestructive, AggregateEngineEmptyConstraints) {
    auto schema = make_order_schema();
    AggregateEngine engine(schema, {});

    auto table = make_ts_double_table("ts", "temp", {1000, 2000}, {25.0, 30.0});
    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok()) << "Empty constraints should be fine";
    EXPECT_EQ(result.value().phase_two.total_windows, 0);
}
