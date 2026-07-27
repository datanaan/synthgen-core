// E2E Pipeline Stress Tests — 4 high-volume scenarios
#include <gtest/gtest.h>

#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "storage/object_store_backend.h"
#include "schema/schema.h"
#include "parser/ast.h"
#include "common/types.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <arrow/builder.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::schema;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::storage;
using ConstraintItem = synthgen::parser::ast::ConstraintItem;
using ConstraintOperator = synthgen::parser::ast::ConstraintOperator;

namespace {

std::filesystem::path make_temp_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

// ============================================================================
// Stress 1: WideTableGeneration — 50 columns, 10000 rows, completes in <30s
// ============================================================================
TEST(PipelineStressTest, WideTableGeneration) {
    // Build a 50-column schema: col_0 through col_49, all FLOAT
    Schema schema;
    schema.type_name = "wide_table";

    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -100.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // One constraint per column to narrow range (optional — just use schema ranges)
    // No extra constraints needed; sampler will use schema ranges directly.
    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 50; ++i) {
        constraints.push_back({
            "col_" + std::to_string(i),
            ConstraintOperator::kBetween,
            -50.0,
            50.0
        });
    }

    auto start = std::chrono::steady_clock::now();

    GenerationRequest gen_req{schema, constraints, 10000, 42, "uniform", 2000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << "Generate failed: " << gen_result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto& gen_data = gen_result.value();
    EXPECT_EQ(gen_data.data->num_rows(), 10000);
    EXPECT_EQ(gen_data.data->num_columns(), 50);

    // Should complete well under 30 seconds
    EXPECT_LT(elapsed_ms, 30000) << "Wide table generation took " << elapsed_ms << "ms";

    // Verify a sample of columns are within constraint range
    for (int ci : {0, 25, 49}) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(
            gen_data.data->column(ci)->chunk(0));
        for (int64_t r = 0; r < std::min(arr->length(), int64_t(100)); ++r) {
            double v = arr->Value(r);
            EXPECT_GE(v, -50.0) << "col_" << ci << " row " << r << " below min";
            EXPECT_LE(v, 50.0) << "col_" << ci << " row " << r << " above max";
        }
    }
}

// ============================================================================
// Stress 2: ManyConstraintsValidation — 20 columns, 20 constraints, 1000 rows
// ============================================================================
TEST(PipelineStressTest, ManyConstraintsValidation) {
    Schema schema;
    schema.type_name = "constrained_table";

    for (int i = 0; i < 20; ++i) {
        ColumnDef col;
        col.name = "val_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -1000.0;
        col.range_max = 1000.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // One constraint per column: each narrows range to [-10, 10]
    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 20; ++i) {
        constraints.push_back({
            "val_" + std::to_string(i),
            ConstraintOperator::kBetween,
            -10.0,
            10.0
        });
    }

    GenerationRequest gen_req{schema, constraints, 1000, 777, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 1000);

    // Validate all 1000 rows against the 20 constraints
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;

    auto& validation = val_result.value();
    EXPECT_EQ(validation.rows_checked, 1000);
    // RectangularSampler generates within constraint range, so all should pass
    EXPECT_EQ(validation.rows_passed, 1000);
    EXPECT_EQ(validation.rows_failed, 0);
    EXPECT_DOUBLE_EQ(validation.pass_rate, 1.0);
}

// ============================================================================
// Stress 3: HundredAppendsWithGeneration — 100 rounds x (10 rows + append)
// ============================================================================
TEST(PipelineStressTest, HundredAppendsWithGeneration) {
    Schema schema;
    schema.type_name = "append_test";

    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    schema.columns.push_back(col);

    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 20.0, 80.0}
    };

    auto dir = make_temp_dir("synthgen_stress_append");
    ObjectStoreBackend backend(dir);
    auto reg = backend.register_table("append_test", "{}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    for (int round = 0; round < 100; ++round) {
        GenerationRequest gen_req{schema, constraints, 10,
                                  static_cast<uint64_t>(round * 100 + 1),
                                  "uniform", 1000};
        RectangularSampler sampler(schema);
        auto gen_result = sampler.generate(gen_req);
        ASSERT_TRUE(gen_result.ok()) << "Generate failed at round " << round
                                     << ": " << gen_result.error().message;

        auto append_result = backend.append("append_test", gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << "Append failed at round " << round
                                        << ": " << append_result.error().message;
    }

    // Verify total rows = 100 * 10 = 1000
    auto scan_result = backend.scan("append_test");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 1000);

    // Verify 100 versions
    auto versions = backend.list_versions("append_test");
    ASSERT_TRUE(versions.ok()) << versions.error().message;
    EXPECT_EQ(versions.value().size(), 100u);

    std::filesystem::remove_all(dir);
}

// ============================================================================
// Stress 4: InterRowLargeStateSequence — 5000 rows, delta=0.001 per step
// ============================================================================
TEST(PipelineStressTest, InterRowLargeStateSequence) {
    // Build schema with ORDER column (DATETIME) + value column (FLOAT)
    Schema schema;
    schema.type_name = "seq_test";

    ColumnDef ts_col;
    ts_col.name = "timestamp";
    ts_col.type = DataType::kDatetime;
    ts_col.not_null = true;
    ts_col.is_order = true;
    schema.columns.push_back(ts_col);

    ColumnDef val_col;
    val_col.name = "value";
    val_col.type = DataType::kFloat;
    val_col.range_min = 0.0;
    val_col.range_max = 100.0;
    schema.columns.push_back(val_col);

    ASSERT_TRUE(schema.validate().ok());

    // Manually build an Arrow table with 5000 rows where value[i] = i * 0.001.
    // The timestamp column is stored as arrow::int64() to match the pattern
    // used by existing inter-row tests (the engine rebuilds filtered rows by
    // type and only handles DOUBLE, INT64, and STRING).
    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder val_builder;

    for (int64_t i = 0; i < 5000; ++i) {
        ASSERT_TRUE(ts_builder.Append(i * 1000000).ok()) << "ts fail at " << i;
        ASSERT_TRUE(val_builder.Append(static_cast<double>(i) * 0.001).ok()) << "val fail at " << i;
    }

    std::shared_ptr<arrow::Array> ts_arr;
    ASSERT_TRUE(ts_builder.Finish(&ts_arr).ok());
    std::shared_ptr<arrow::Array> val_arr;
    ASSERT_TRUE(val_builder.Finish(&val_arr).ok());

    auto arrow_schema = arrow::schema({
        arrow::field("timestamp", arrow::int64()),
        arrow::field("value", arrow::float64())
    });
    auto table = arrow::Table::Make(arrow_schema,
        {std::make_shared<arrow::ChunkedArray>(ts_arr),
         std::make_shared<arrow::ChunkedArray>(val_arr)});
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->num_rows(), 5000);

    // Set up InterRowEngine with delta_max=10.0
    InterRowConstraintDef ir_def;
    ir_def.column_name = "value";
    ir_def.order_column = "timestamp";
    ir_def.type = InterRowConstraintDef::Type::kDeltaMax;
    ir_def.delta_max = 10.0;

    std::vector<InterRowConstraintDef> ir_constraints = {ir_def};

    InterRowEngine engine(schema, ir_constraints);
    std::vector<InterRowState> incoming_states;  // no prior state
    auto result = engine.execute_batch(table, incoming_states);
    ASSERT_TRUE(result.ok()) << "InterRowEngine failed: " << result.error().message;

    auto& ir_result = result.value();
    // All 5000 rows should pass since each step delta is 0.001 < 10.0
    EXPECT_EQ(ir_result.rows_passed, 5000);
    EXPECT_EQ(ir_result.rows_filtered, 0);
    EXPECT_EQ(ir_result.filtered_batch->num_rows(), 5000);
}
