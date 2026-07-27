// Storage + Engine integration tests — 6 scenarios validating generation-to-storage pipelines
#include <gtest/gtest.h>

#include "storage/object_store_backend.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "schema/schema.h"
#include "parser/ast.h"
#include "common/types.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::storage;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;
using ConstraintItem = synthgen::parser::ast::ConstraintItem;
using ConstraintOperator = synthgen::parser::ast::ConstraintOperator;

namespace {

std::filesystem::path make_temp_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

Schema make_sensor_schema() {
    Schema schema;
    schema.type_name = "sensor_data";

    ColumnDef temp_col;
    temp_col.name = "temperature";
    temp_col.type = DataType::kFloat;
    temp_col.range_min = -50.0;
    temp_col.range_max = 80.0;
    schema.columns.push_back(temp_col);

    ColumnDef speed_col;
    speed_col.name = "wind_speed";
    speed_col.type = DataType::kFloat;
    speed_col.range_min = 0.0;
    speed_col.range_max = 50.0;
    schema.columns.push_back(speed_col);

    return schema;
}

} // namespace

// ============================================================================
// Test 1: Generate 500 rows, append to storage, scan, verify count and range
// ============================================================================
TEST(StorageEngineTest, GenerateStoreReadback) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, -10.0, 45.0},
        {"wind_speed", ConstraintOperator::kBetween, 5.0, 30.0}
    };

    GenerationRequest gen_req{schema, constraints, 500, 42, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto& gen_data = gen_result.value();
    EXPECT_EQ(gen_data.data->num_rows(), 500);
    EXPECT_EQ(gen_data.data->num_columns(), 2);

    // Store in backend
    auto dir = make_temp_dir("synthgen_se_test1");
    ObjectStoreBackend backend(dir);
    auto reg = backend.register_table("sensor_data", "{}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    auto append_result = backend.append("sensor_data", gen_data.data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Scan back
    auto scan_result = backend.scan("sensor_data");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto table = scan_result.value();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 500);

    // Verify values are within constraint range
    auto temp_col = table->GetColumnByName("temperature");
    auto speed_col = table->GetColumnByName("wind_speed");
    ASSERT_NE(temp_col, nullptr);
    ASSERT_NE(speed_col, nullptr);

    auto temp_arr = std::static_pointer_cast<arrow::DoubleArray>(temp_col->chunk(0));
    auto speed_arr = std::static_pointer_cast<arrow::DoubleArray>(speed_col->chunk(0));

    for (int64_t i = 0; i < temp_arr->length(); ++i) {
        double t = temp_arr->Value(i);
        EXPECT_GE(t, -10.0) << "temperature out of range at row " << i;
        EXPECT_LE(t, 45.0) << "temperature out of range at row " << i;
    }
    for (int64_t i = 0; i < speed_arr->length(); ++i) {
        double s = speed_arr->Value(i);
        EXPECT_GE(s, 5.0) << "wind_speed out of range at row " << i;
        EXPECT_LE(s, 30.0) << "wind_speed out of range at row " << i;
    }
}

// ============================================================================
// Test 2: Generate with constraints, validate, store, scan, verify
// ============================================================================
TEST(StorageEngineTest, GenerateValidateFilterStore) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, 0.0, 40.0},
        {"wind_speed", ConstraintOperator::kBetween, 10.0, 25.0}
    };

    GenerationRequest gen_req{schema, constraints, 300, 77, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // Validate with ValueRangeValidator
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;
    auto& validation = val_result.value();
    EXPECT_EQ(validation.rows_checked, 300);
    // All rows should pass since RectangularSampler generates within constraint range
    EXPECT_EQ(validation.rows_passed, 300);
    EXPECT_EQ(validation.rows_failed, 0);

    // Store
    auto dir = make_temp_dir("synthgen_se_test2");
    ObjectStoreBackend backend(dir);
    backend.register_table("validated_data", "{}");
    auto append_result = backend.append("validated_data", gen_result.value().data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Scan and verify most values are in constraint range
    auto scan_result = backend.scan("validated_data");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto table = scan_result.value();
    EXPECT_EQ(table->num_rows(), 300);

    auto temp_arr = std::static_pointer_cast<arrow::DoubleArray>(
        table->GetColumnByName("temperature")->chunk(0));
    int in_range = 0;
    for (int64_t i = 0; i < temp_arr->length(); ++i) {
        double v = temp_arr->Value(i);
        if (v >= 0.0 && v <= 40.0) ++in_range;
    }
    // At least 90% should be in constraint range (tolerance for edge effects)
    EXPECT_GE(in_range, static_cast<int>(temp_arr->length() * 0.9));
}

// ============================================================================
// Test 3: 5 rounds of generation (100 rows each) append to same table = 500 rows, 5 versions
// ============================================================================
TEST(StorageEngineTest, MultipleGenerationsAppendToSameTable) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, -20.0, 60.0},
        {"wind_speed", ConstraintOperator::kBetween, 0.0, 40.0}
    };

    auto dir = make_temp_dir("synthgen_se_test3");
    ObjectStoreBackend backend(dir);
    backend.register_table("multi_gen", "{}");

    for (int round = 0; round < 5; ++round) {
        GenerationRequest gen_req{schema, constraints, 100,
                                  static_cast<uint64_t>(round * 1000 + 1), "uniform", 1000};
        RectangularSampler sampler(schema);
        auto gen_result = sampler.generate(gen_req);
        ASSERT_TRUE(gen_result.ok()) << "Generate failed at round " << round
                                     << ": " << gen_result.error().message;

        auto append_result = backend.append("multi_gen", gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << "Append failed at round " << round
                                        << ": " << append_result.error().message;
    }

    // Verify total rows
    auto scan_result = backend.scan("multi_gen");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 500);

    // Verify 5 versions
    auto versions = backend.list_versions("multi_gen");
    ASSERT_TRUE(versions.ok()) << versions.error().message;
    EXPECT_EQ(versions.value().size(), 5u);

    // Each version should have 100 rows
    for (size_t i = 0; i < versions.value().size(); ++i) {
        EXPECT_EQ(versions.value()[i].row_count, 100)
            << "Version " << i << " has wrong row count";
    }
}

// ============================================================================
// Test 4: Generate 2-column data, store, scan only 1 column, verify
// ============================================================================
TEST(StorageEngineTest, ColumnProjectionAfterGeneration) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, -5.0, 35.0},
        {"wind_speed", ConstraintOperator::kBetween, 2.0, 20.0}
    };

    GenerationRequest gen_req{schema, constraints, 200, 55, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_columns(), 2);

    auto dir = make_temp_dir("synthgen_se_test4");
    ObjectStoreBackend backend(dir);
    backend.register_table("proj_test", "{}");
    backend.append("proj_test", gen_result.value().data);

    // Scan only the temperature column
    auto scan_result = backend.scan("proj_test", {"temperature"});
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto table = scan_result.value();
    EXPECT_EQ(table->num_columns(), 1);
    EXPECT_EQ(table->num_rows(), 200);
    EXPECT_EQ(table->schema()->field(0)->name(), "temperature");

    // Verify values are in range
    auto arr = std::static_pointer_cast<arrow::DoubleArray>(
        table->column(0)->chunk(0));
    for (int64_t i = 0; i < arr->length(); ++i) {
        double v = arr->Value(i);
        EXPECT_GE(v, -5.0) << "temperature out of range at row " << i;
        EXPECT_LE(v, 35.0) << "temperature out of range at row " << i;
    }
}

// ============================================================================
// Test 5: Generate 1000 rows, store, scan with range predicate, verify filtered
// ============================================================================
TEST(StorageEngineTest, ScanWithPredicateOnGeneratedData) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, -50.0, 80.0},
        {"wind_speed", ConstraintOperator::kBetween, 0.0, 50.0}
    };

    GenerationRequest gen_req{schema, constraints, 1000, 123, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    auto dir = make_temp_dir("synthgen_se_test5");
    ObjectStoreBackend backend(dir);
    backend.register_table("pred_data", "{}");
    backend.append("pred_data", gen_result.value().data);

    // Full scan should return all rows
    auto full_scan = backend.scan("pred_data");
    ASSERT_TRUE(full_scan.ok()) << full_scan.error().message;
    EXPECT_EQ(full_scan.value()->num_rows(), 1000);

    // Scan with predicate: temperature between 0 and 40
    ScanPredicate pred;
    pred.column = "temperature";
    pred.min_value = 0.0;
    pred.max_value = 40.0;

    auto filtered_scan = backend.scan("pred_data", {}, pred);
    ASSERT_TRUE(filtered_scan.ok()) << filtered_scan.error().message;
    auto filtered = filtered_scan.value();
    EXPECT_GT(filtered->num_rows(), 0);
    EXPECT_LT(filtered->num_rows(), 1000);  // Should filter out some rows

    // Verify all returned values are within predicate range
    auto temp_col = filtered->GetColumnByName("temperature");
    ASSERT_NE(temp_col, nullptr);
    auto arr = std::static_pointer_cast<arrow::DoubleArray>(temp_col->chunk(0));
    for (int64_t i = 0; i < arr->length(); ++i) {
        double v = arr->Value(i);
        EXPECT_GE(v, 0.0) << "Filtered value below min at row " << i;
        EXPECT_LE(v, 40.0) << "Filtered value above max at row " << i;
    }
}

// ============================================================================
// Test 6: Generate + store, destroy backend, create new backend, verify data persists
// ============================================================================
TEST(StorageEngineTest, RestartPersistenceWithGeneratedData) {
    auto schema = make_sensor_schema();
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"temperature", ConstraintOperator::kBetween, -10.0, 50.0},
        {"wind_speed", ConstraintOperator::kBetween, 1.0, 35.0}
    };

    GenerationRequest gen_req{schema, constraints, 250, 888, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // Save values for later verification
    auto orig_temp = std::static_pointer_cast<arrow::DoubleArray>(
        gen_result.value().data->GetColumnByName("temperature")->chunk(0));
    double first_temp = orig_temp->Value(0);
    double last_temp = orig_temp->Value(orig_temp->length() - 1);

    auto dir = make_temp_dir("synthgen_se_test6");
    std::string table_id = "persistent_data";

    // Phase 1: Write data
    {
        ObjectStoreBackend backend(dir);
        auto reg = backend.register_table(table_id, "{\"type\":\"sensor\"}");
        ASSERT_TRUE(reg.ok()) << reg.error().message;

        auto append_result = backend.append(table_id, gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << append_result.error().message;

        auto scan = backend.scan(table_id);
        ASSERT_TRUE(scan.ok()) << scan.error().message;
        EXPECT_EQ(scan.value()->num_rows(), 250);
    }

    // Phase 2: Simulate restart — new backend instance, same directory
    {
        ObjectStoreBackend backend2(dir);

        // Table should still exist
        auto has = backend2.has_table(table_id);
        ASSERT_TRUE(has.ok()) << has.error().message;
        EXPECT_TRUE(has.value());

        // Data should persist
        auto scan = backend2.scan(table_id);
        ASSERT_TRUE(scan.ok()) << scan.error().message;
        EXPECT_EQ(scan.value()->num_rows(), 250);

        // Verify first and last values match original generated data
        auto read_temp = std::static_pointer_cast<arrow::DoubleArray>(
            scan.value()->GetColumnByName("temperature")->chunk(0));
        EXPECT_DOUBLE_EQ(read_temp->Value(0), first_temp);
        EXPECT_DOUBLE_EQ(read_temp->Value(read_temp->length() - 1), last_temp);

        // Versions should persist
        auto versions = backend2.list_versions(table_id);
        ASSERT_TRUE(versions.ok()) << versions.error().message;
        EXPECT_EQ(versions.value().size(), 1u);
        EXPECT_EQ(versions.value()[0].row_count, 250);
    }
}
