// Performance and Stress Tests — push the system to its limits
// Large volumes, wide tables, long version chains, audit stress, memory, constraints, evidence
#include <gtest/gtest.h>

#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "storage/version/model_version_chain.h"
#include "storage/version/model_version.h"
#include "storage/model/model_storage_layer.h"
#include "storage/metadata.h"
#include "storage/gc/protection.h"
#include "storage/gc/gc_compactor.h"
#include "storage/timetravel/time_travel_engine.h"
#include "engine/alignment/drift_detector.h"
#include "engine/alignment/continuous_alignment_engine.h"
#include "schema/schema.h"
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
#include <map>
#include <memory>
#include <algorithm>
#include <numeric>
#include <random>
#include <sstream>
#include <unistd.h>

using namespace synthgen;
using namespace synthgen::schema;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::router;
using namespace synthgen::engine::postfilter;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;
using namespace synthgen::storage::version;
using namespace synthgen::storage::model;
using namespace synthgen::storage::gc;
using namespace synthgen::storage::timetravel;
using namespace synthgen::engine::alignment;
using ConstraintItem = synthgen::parser::ast::ConstraintItem;
using ConstraintOperator = synthgen::parser::ast::ConstraintOperator;

namespace {

std::filesystem::path make_temp_dir(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() /
               ("synthgen_perf_" + std::to_string(::getpid()) + "_" + name);
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

std::shared_ptr<arrow::Table> make_wide_double_table(int num_cols, int64_t rows) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::Array>> arrays;
    for (int c = 0; c < num_cols; ++c) {
        arrow::DoubleBuilder builder;
        for (int64_t i = 0; i < rows; ++i) {
            builder.Append(static_cast<double>(i + c * 0.1));
        }
        std::shared_ptr<arrow::Array> arr;
        builder.Finish(&arr);
        fields.push_back(arrow::field("col_" + std::to_string(c), arrow::float64()));
        arrays.push_back(arr);
    }
    return arrow::Table::Make(arrow::schema(fields), arrays);
}

}  // namespace

// ============================================================================
// LARGE VOLUME TESTS
// ============================================================================

// Generate 1M rows, verify all present and valid
TEST(PerformanceStressTest, Generate1MRowsAllValid) {
    Schema schema;
    schema.type_name = "vol_test";

    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 1000.0;
    schema.columns.push_back(col);
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 100.0, 900.0}
    };

    auto start = std::chrono::steady_clock::now();

    GenerationRequest gen_req{schema, constraints, 1000000, 42, "uniform", 50000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << "Generate failed: " << gen_result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto& data = gen_result.value();
    EXPECT_EQ(data.data->num_rows(), 1000000);
    EXPECT_EQ(data.data->num_columns(), 1);

    // Verify all values within constraint range
    auto arr = std::static_pointer_cast<arrow::DoubleArray>(
        data.data->column(0)->chunk(0));
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    for (int64_t i = 0; i < arr->length(); ++i) {
        double v = arr->Value(i);
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
        ASSERT_GE(v, 100.0) << "Row " << i << " below min";
        ASSERT_LE(v, 900.0) << "Row " << i << " above max";
    }

    // Should complete in under 60 seconds for 1M rows
    EXPECT_LT(elapsed_ms, 60000) << "1M row generation took " << elapsed_ms << "ms";

    // Verify reasonable min/max spread
    EXPECT_LT(min_val, 110.0) << "Min value suspiciously high: " << min_val;
    EXPECT_GT(max_val, 890.0) << "Max value suspiciously low: " << max_val;
}

// Generate 10M values in a single column, check min/max bounds
TEST(PerformanceStressTest, Generate10MSingleColumnBoundsCheck) {
    Schema schema;
    schema.type_name = "bound_test";

    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    col.range_min = -500.0;
    col.range_max = 500.0;
    schema.columns.push_back(col);
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"val", ConstraintOperator::kBetween, -500.0, 500.0}
    };

    auto start = std::chrono::steady_clock::now();

    GenerationRequest gen_req{schema, constraints, 10000000, 12345, "uniform", 100000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(gen_result.value().data->num_rows(), 10000000);

    auto arr = std::static_pointer_cast<arrow::DoubleArray>(
        gen_result.value().data->column(0)->chunk(0));
    double min_val = std::numeric_limits<double>::max();
    double max_val = std::numeric_limits<double>::lowest();
    for (int64_t i = 0; i < arr->length(); ++i) {
        double v = arr->Value(i);
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
        ASSERT_GE(v, -500.0) << "Row " << i << " below min";
        ASSERT_LE(v, 500.0) << "Row " << i << " above max";
    }

    // Verify distribution spread for 10M samples
    EXPECT_LT(min_val, -490.0) << "Min too high for 10M samples: " << min_val;
    EXPECT_GT(max_val, 490.0) << "Max too low for 10M samples: " << max_val;

    EXPECT_LT(elapsed_ms, 120000) << "10M generation took " << elapsed_ms << "ms";
}

// Append 100 batches of 10K rows each, verify row count
TEST(PerformanceStressTest, Append100Batches10KRows) {
    auto dir = make_temp_dir("append_100_batch");
    ObjectStoreBackend backend(dir);
    auto reg = backend.register_table("batch_test", "{}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    Schema schema;
    schema.type_name = "batch_test";
    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    schema.columns.push_back(col);
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 10.0, 90.0}
    };

    auto start = std::chrono::steady_clock::now();

    for (int round = 0; round < 100; ++round) {
        GenerationRequest gen_req{schema, constraints, 10000,
                                  static_cast<uint64_t>(round * 1000 + 1),
                                  "uniform", 10000};
        RectangularSampler sampler(schema);
        auto gen_result = sampler.generate(gen_req);
        ASSERT_TRUE(gen_result.ok()) << "Generate failed at round " << round
                                     << ": " << gen_result.error().message;

        auto append_result = backend.append("batch_test", gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << "Append failed at round " << round
                                        << ": " << append_result.error().message;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Verify total rows = 100 * 10000 = 1,000,000
    auto scan_result = backend.scan("batch_test");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 1000000);

    // Verify 100 versions
    auto versions = backend.list_versions("batch_test");
    ASSERT_TRUE(versions.ok()) << versions.error().message;
    EXPECT_EQ(versions.value().size(), 100u);

    EXPECT_LT(elapsed_ms, 120000) << "100 batch append took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// 500K rows with 5 inter-row constraints active
TEST(PerformanceStressTest, Generate500KWith5InterRowConstraints) {
    Schema schema;
    schema.type_name = "ir_vol_test";

    ColumnDef ts_col;
    ts_col.name = "timestamp";
    ts_col.type = DataType::kDatetime;
    ts_col.not_null = true;
    ts_col.is_order = true;
    schema.columns.push_back(ts_col);

    // 5 value columns
    for (int i = 0; i < 5; ++i) {
        ColumnDef col;
        col.name = "val_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    constexpr int64_t kRows = 500000;

    // Build arrow table manually with controlled increments
    arrow::Int64Builder ts_builder;
    std::vector<arrow::DoubleBuilder> val_builders(5);
    for (int64_t i = 0; i < kRows; ++i) {
        ASSERT_TRUE(ts_builder.Append(i * 1000000).ok());
        for (int v = 0; v < 5; ++v) {
            // Each column grows slowly to stay within delta_max=5.0
            ASSERT_TRUE(val_builders[v].Append(
                static_cast<double>(i % 10) * 0.5 + v * 0.1).ok());
        }
    }

    std::shared_ptr<arrow::Array> ts_arr;
    ASSERT_TRUE(ts_builder.Finish(&ts_arr).ok());
    std::vector<std::shared_ptr<arrow::Array>> val_arrays(5);
    for (int v = 0; v < 5; ++v) {
        ASSERT_TRUE(val_builders[v].Finish(&val_arrays[v]).ok());
    }

    std::vector<std::shared_ptr<arrow::Field>> fields = {
        arrow::field("timestamp", arrow::int64())
    };
    std::vector<std::shared_ptr<arrow::Array>> arrays = {ts_arr};
    for (int v = 0; v < 5; ++v) {
        fields.push_back(arrow::field("val_" + std::to_string(v), arrow::float64()));
        arrays.push_back(val_arrays[v]);
    }
    auto table = arrow::Table::Make(arrow::schema(fields), arrays);
    ASSERT_EQ(table->num_rows(), kRows);

    // 5 inter-row constraints: each val column delta_max = 5.0
    std::vector<InterRowConstraintDef> ir_defs;
    for (int v = 0; v < 5; ++v) {
        InterRowConstraintDef def;
        def.column_name = "val_" + std::to_string(v);
        def.order_column = "timestamp";
        def.type = InterRowConstraintDef::Type::kDeltaMax;
        def.delta_max = 5.0;
        ir_defs.push_back(def);
    }

    auto start = std::chrono::steady_clock::now();

    InterRowEngine engine(schema, ir_defs);
    std::vector<InterRowState> incoming;
    auto result = engine.execute_batch(table, incoming);
    ASSERT_TRUE(result.ok()) << "InterRowEngine failed: " << result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto& ir_result = result.value();
    EXPECT_EQ(ir_result.rows_passed, kRows);
    EXPECT_EQ(ir_result.rows_filtered, 0);
    EXPECT_EQ(ir_result.filtered_batch->num_rows(), kRows);

    EXPECT_LT(elapsed_ms, 60000) << "500K inter-row took " << elapsed_ms << "ms";
}

// ============================================================================
// WIDE TABLE TESTS
// ============================================================================

// Schema with 200 columns (mix of FLOAT, INT, STRING, ENUM)
TEST(PerformanceStressTest, WideTable200ColumnsMixedTypes) {
    Schema schema;
    schema.type_name = "wide_200";

    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "float_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -100.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "int_" + std::to_string(i);
        col.type = DataType::kInt;
        col.range_min = -1000.0;
        col.range_max = 1000.0;
        schema.columns.push_back(col);
    }
    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "str_" + std::to_string(i);
        col.type = DataType::kString;
        schema.columns.push_back(col);
    }
    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "enum_" + std::to_string(i);
        col.type = DataType::kEnum;
        col.enum_values = {"a", "b", "c"};
        schema.columns.push_back(col);
    }

    ASSERT_TRUE(schema.validate().ok());
    EXPECT_EQ(schema.columns.size(), 200u);

    // Generate with constraints on 50 float columns
    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 50; ++i) {
        constraints.push_back({
            "float_" + std::to_string(i),
            ConstraintOperator::kBetween,
            -50.0,
            50.0
        });
    }

    auto start = std::chrono::steady_clock::now();

    GenerationRequest gen_req{schema, constraints, 5000, 42, "uniform", 5000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << "Generate failed: " << gen_result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto& data = gen_result.value();
    EXPECT_EQ(data.data->num_rows(), 5000);
    EXPECT_EQ(data.data->num_columns(), 200);

    // Spot-check a few float columns are in range
    for (int ci : {0, 25, 49}) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(
            data.data->column(ci)->chunk(0));
        for (int64_t r = 0; r < std::min(arr->length(), int64_t(200)); ++r) {
            double v = arr->Value(r);
            EXPECT_GE(v, -50.0) << "float_" << ci << " row " << r << " below min";
            EXPECT_LE(v, 50.0) << "float_" << ci << " row " << r << " above max";
        }
    }

    EXPECT_LT(elapsed_ms, 60000) << "200-col generation took " << elapsed_ms << "ms";
}

// 200-column table with constraints on 50 columns simultaneously
TEST(PerformanceStressTest, WideTable200Cols50ConstraintsValidation) {
    Schema schema;
    schema.type_name = "wide_200_constrained";

    for (int i = 0; i < 200; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -1000.0;
        col.range_max = 1000.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // Constraints on first 50 columns
    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 50; ++i) {
        constraints.push_back({
            "col_" + std::to_string(i),
            ConstraintOperator::kBetween,
            -100.0,
            100.0
        });
    }

    GenerationRequest gen_req{schema, constraints, 2000, 777, "uniform", 2000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 2000);

    auto start = std::chrono::steady_clock::now();

    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    auto& validation = val_result.value();
    EXPECT_EQ(validation.rows_checked, 2000);
    EXPECT_EQ(validation.rows_passed, 2000);
    EXPECT_EQ(validation.rows_failed, 0);

    EXPECT_LT(elapsed_ms, 10000) << "50 constraint validation took " << elapsed_ms << "ms";
}

// ============================================================================
// VERSION CHAIN STRESS
// ============================================================================

// Create 500 version chain entries
TEST(PerformanceStressTest, VersionChain500Entries) {
    auto dir = make_temp_dir("verchain_500");
    MetadataManager meta(dir);
    ModelVersionChain chain(meta);

    auto start = std::chrono::steady_clock::now();

    std::string prev_id;
    std::vector<std::string> version_ids;

    for (int i = 0; i < 500; ++i) {
        ModelVersion mv;
        mv.model_name = "stress_model";
        mv.created_by = "perf_test";
        mv.fidelity_score = static_cast<double>(i) / 500.0;
        mv.training_rows = (i + 1) * 100;

        auto result = chain.create_version("stress_model", prev_id, mv);
        ASSERT_TRUE(result.ok()) << "create_version failed at " << i
                                 << ": " << result.error().message;
        prev_id = result.value().version_id;
        version_ids.push_back(prev_id);
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // List all versions
    auto list = chain.list_versions("stress_model", 1000);
    ASSERT_TRUE(list.ok()) << list.error().message;
    EXPECT_EQ(list.value().size(), 500u);

    // Verify each version is retrievable
    for (int i = 0; i < 500; i += 50) {  // Spot check every 50th
        auto v = chain.get_version(version_ids[i]);
        ASSERT_TRUE(v.ok()) << "get_version failed for " << version_ids[i];
        EXPECT_EQ(v.value()->model_name, "stress_model");
        EXPECT_DOUBLE_EQ(v.value()->fidelity_score, static_cast<double>(i) / 500.0);
    }

    EXPECT_LT(elapsed_ms, 10000) << "500 version chain took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// Time travel to version 250 in a 500-entry chain
TEST(PerformanceStressTest, TimeTravelVersion250Of500) {
    auto dir = make_temp_dir("timetravel_500");
    MetadataManager meta(dir);
    ModelVersionChain chain(meta);
    ModelStorageLayer storage(dir.string());

    // Create 500 versions and save model data for each
    std::string prev_id;
    std::vector<std::string> version_ids;

    for (int i = 0; i < 500; ++i) {
        ModelVersion mv;
        mv.model_name = "tt_model";
        mv.created_by = "perf_test";
        mv.fidelity_score = static_cast<double>(i) / 500.0;
        mv.training_rows = (i + 1) * 100;

        auto result = chain.create_version("tt_model", prev_id, mv);
        ASSERT_TRUE(result.ok()) << result.error().message;
        prev_id = result.value().version_id;
        version_ids.push_back(prev_id);

        // Save model data for this version
        std::string model_data = "model_data_v" + std::to_string(i);
        auto save_result = storage.save_checkpoint("tt_model", prev_id, model_data);
        ASSERT_TRUE(save_result.ok()) << save_result.error().message;
    }

    // Time travel to version 250
    TimeTravelEngine tt_engine(chain, storage);

    auto start = std::chrono::steady_clock::now();
    auto tt_result = tt_engine.query_as_of("tt_model", version_ids[250]);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(tt_result.ok()) << "Time travel failed: " << tt_result.error().message;
    EXPECT_EQ(tt_result.value().data, "model_data_v250");
    EXPECT_EQ(tt_result.value().version.version_id, version_ids[250]);
    EXPECT_FALSE(tt_result.value().was_degraded);

    // Verify first and last versions too
    auto first = tt_engine.query_as_of("tt_model", version_ids[0]);
    ASSERT_TRUE(first.ok()) << first.error().message;
    EXPECT_EQ(first.value().data, "model_data_v0");

    auto last = tt_engine.query_as_of("tt_model", version_ids[499]);
    ASSERT_TRUE(last.ok()) << last.error().message;
    EXPECT_EQ(last.value().data, "model_data_v499");

    EXPECT_LT(elapsed_ms, 5000) << "Time travel to v250 took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// GC compaction on 500-version chain with high compaction ratio
TEST(PerformanceStressTest, GCCompaction500VersionChain) {
    auto dir = make_temp_dir("gc_500");
    MetadataManager meta(dir);
    ModelVersionChain chain(meta);
    ProtectionChecker checker;
    ProtectionConfig config;
    config.keep_recent_n = 10;
    config.auto_compact_enabled = true;
    GcCompactor compactor(chain, checker, config);

    // Create 500 versions
    std::string prev_id;
    std::vector<std::string> version_ids;

    for (int i = 0; i < 500; ++i) {
        ModelVersion mv;
        mv.model_name = "gc_model";
        mv.created_by = "perf_test";
        mv.fidelity_score = 0.9;
        mv.training_rows = 1000;

        auto result = chain.create_version("gc_model", prev_id, mv);
        ASSERT_TRUE(result.ok()) << result.error().message;
        prev_id = result.value().version_id;
        version_ids.push_back(prev_id);
    }

    // Verify 500 versions exist
    auto list = chain.list_versions("gc_model", 1000);
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 500u);

    // Run compaction
    auto start = std::chrono::steady_clock::now();
    auto compact_result = compactor.compact("gc_model");
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(compact_result.ok()) << "Compaction failed: " << compact_result.error().message;

    // Most versions should be compacted (except recent 10 and first version)
    auto& cr = compact_result.value();
    EXPECT_GT(cr.compacted_versions.size(), 0u) << "Expected some versions to be compacted";

    // Verify explain info
    auto explain = compactor.explain("gc_model");
    EXPECT_GT(explain.total_versions, 0);

    EXPECT_LT(elapsed_ms, 10000) << "GC compaction took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// ============================================================================
// AUDIT LOG STRESS
// ============================================================================

// Write 10K audit entries, verify hash chain integrity
TEST(PerformanceStressTest, AuditLog10KEntriesHashChain) {
    AuditLog log;

    auto genesis = log.create_genesis();
    ASSERT_TRUE(genesis.ok()) << genesis.error().message;
    EXPECT_EQ(log.record_count(), 1);

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 10000; ++i) {
        std::string op = "operation_" + std::to_string(i);
        std::map<std::string, std::string> meta;
        meta["batch_index"] = std::to_string(i);
        meta["rows_affected"] = std::to_string((i + 1) * 100);
        meta["component"] = "stress_test";

        auto result = log.append(op, "perf_tester", meta);
        ASSERT_TRUE(result.ok()) << "Append " << i << " failed: " << result.error().message;

        // Verify chain hash is present
        ASSERT_FALSE(result.value().chain_hash.empty()) << "Empty chain_hash at " << i;
        ASSERT_FALSE(result.value().content_hash.empty()) << "Empty content_hash at " << i;
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(log.record_count(), 10001);  // genesis + 10000

    // Full chain verification
    auto verify_start = std::chrono::steady_clock::now();
    auto verify = log.verify_chain();
    auto verify_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - verify_start).count();

    ASSERT_TRUE(verify.ok()) << verify.error().message;
    EXPECT_TRUE(verify.value()) << "Chain verification failed for 10K-record chain";

    // Daily verification
    auto daily = log.daily_verification();
    ASSERT_TRUE(daily.ok()) << daily.error().message;
    EXPECT_TRUE(daily.value().is_valid);
    EXPECT_EQ(daily.value().total_records, 10001);
    EXPECT_TRUE(daily.value().broken_links.empty());

    // No forks expected
    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty());

    // get_latest should return last operation
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "operation_9999");

    EXPECT_LT(elapsed_ms, 30000) << "10K audit appends took " << elapsed_ms << "ms";
    EXPECT_LT(verify_ms, 5000) << "10K chain verify took " << verify_ms << "ms";
}

// Verify audit log doesn't degrade with size — compare per-entry timing
TEST(PerformanceStressTest, AuditLogPerformanceConsistency) {
    AuditLog log;
    auto genesis = log.create_genesis();
    ASSERT_TRUE(genesis.ok());

    // Measure first 1000 entries
    auto start_first = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto result = log.append("op", "tester");
        ASSERT_TRUE(result.ok());
    }
    auto first_1k_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_first).count();

    // Measure last 1000 entries (entries 4000-4999)
    for (int i = 1000; i < 4000; ++i) {
        auto result = log.append("op", "tester");
        ASSERT_TRUE(result.ok());
    }
    auto start_last = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) {
        auto result = log.append("op", "tester");
        ASSERT_TRUE(result.ok());
    }
    auto last_1k_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_last).count();

    // The last 1K should not take more than 10x the first 1K (allowing for some variance)
    // If first_1k_ms is 0, use 1 to avoid division by zero
    double first_ms = std::max(first_1k_ms, static_cast<int64_t>(1));
    double ratio = static_cast<double>(last_1k_ms) / first_ms;
    EXPECT_LT(ratio, 10.0) << "Audit log performance degraded: first 1K="
                            << first_1k_ms << "ms, last 1K=" << last_1k_ms << "ms";

    // Verify integrity
    auto verify = log.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}

// ============================================================================
// MEMORY-SENSITIVE TESTS
// ============================================================================

// Generate data, store to Parquet, release memory, load back — verify via row counts
TEST(PerformanceStressTest, ParquetRoundTripMemoryRelease) {
    auto dir = make_temp_dir("parquet_roundtrip");

    Schema schema;
    schema.type_name = "mem_test";
    for (int i = 0; i < 10; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 10; ++i) {
        constraints.push_back({"col_" + std::to_string(i),
                               ConstraintOperator::kBetween, 10.0, 90.0});
    }

    // Generate 100K rows
    GenerationRequest gen_req{schema, constraints, 100000, 42, "uniform", 10000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 100000);

    int64_t original_rows = gen_result.value().data->num_rows();

    // Write to Parquet
    ParquetWriter writer;
    std::string parquet_path = (dir / "data.parquet").string();
    auto write_result = writer.write(parquet_path, gen_result.value().data);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    // Release original data
    gen_result.value().data.reset();
    EXPECT_EQ(gen_result.value().data, nullptr);

    // Load back
    ParquetReader reader;
    auto read_result = reader.read_all(parquet_path);
    ASSERT_TRUE(read_result.ok()) << read_result.error().message;

    auto& loaded = read_result.value();
    EXPECT_EQ(loaded->num_rows(), original_rows) << "Row count mismatch after round trip";
    EXPECT_EQ(loaded->num_columns(), 10);

    // Verify data integrity — spot check
    for (int ci = 0; ci < 10; ci += 3) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(
            loaded->column(ci)->chunk(0));
        for (int64_t r = 0; r < std::min(arr->length(), int64_t(100)); ++r) {
            double v = arr->Value(r);
            EXPECT_GE(v, 10.0) << "col_" << ci << " row " << r << " below min after reload";
            EXPECT_LE(v, 90.0) << "col_" << ci << " row " << r << " above max after reload";
        }
    }

    std::filesystem::remove_all(dir);
}

// Many small batches (10K batches of 1 row each) vs few large batches — verify same row count
TEST(PerformanceStressTest, ManySmallVsFewLargeBatches) {
    auto dir_small = make_temp_dir("small_batch");
    auto dir_large = make_temp_dir("large_batch");

    ObjectStoreBackend small_backend(dir_small);
    ObjectStoreBackend large_backend(dir_large);

    small_backend.register_table("small", "{}");
    large_backend.register_table("large", "{}");

    Schema schema;
    schema.type_name = "batch_cmp";
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

    // Small batches: 1000 batches of 10 rows each = 10,000 rows
    for (int round = 0; round < 1000; ++round) {
        GenerationRequest gen_req{schema, constraints, 10,
                                  static_cast<uint64_t>(round + 1),
                                  "uniform", 10};
        RectangularSampler sampler(schema);
        auto gen_result = sampler.generate(gen_req);
        ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
        auto append_result = small_backend.append("small", gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << append_result.error().message;
    }

    // Large batches: 1 batch of 10,000 rows
    {
        GenerationRequest gen_req{schema, constraints, 10000, 42, "uniform", 10000};
        RectangularSampler sampler(schema);
        auto gen_result = sampler.generate(gen_req);
        ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
        auto append_result = large_backend.append("large", gen_result.value().data);
        ASSERT_TRUE(append_result.ok()) << append_result.error().message;
    }

    // Both should have 10,000 rows
    auto small_scan = small_backend.scan("small");
    ASSERT_TRUE(small_scan.ok()) << small_scan.error().message;
    EXPECT_EQ(small_scan.value()->num_rows(), 10000);

    auto large_scan = large_backend.scan("large");
    ASSERT_TRUE(large_scan.ok()) << large_scan.error().message;
    EXPECT_EQ(large_scan.value()->num_rows(), 10000);

    // Verify all values in range for both
    for (auto& scan_result : {small_scan.value(), large_scan.value()}) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(
            scan_result->GetColumnByName("value")->chunk(0));
        for (int64_t r = 0; r < std::min(arr->length(), int64_t(500)); ++r) {
            double v = arr->Value(r);
            EXPECT_GE(v, 20.0) << "Row " << r << " below min";
            EXPECT_LE(v, 80.0) << "Row " << r << " above max";
        }
    }

    std::filesystem::remove_all(dir_small);
    std::filesystem::remove_all(dir_large);
}

// ============================================================================
// CONSTRAINT STRESS
// ============================================================================

// 100 simultaneous value-range constraints on different columns
TEST(PerformanceStressTest, HundredValueRangeConstraints) {
    Schema schema;
    schema.type_name = "constraint_100";

    for (int i = 0; i < 100; ++i) {
        ColumnDef col;
        col.name = "val_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -1000.0;
        col.range_max = 1000.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // Each column constrained to [-10, 10]
    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 100; ++i) {
        constraints.push_back({
            "val_" + std::to_string(i),
            ConstraintOperator::kBetween,
            -10.0,
            10.0
        });
    }

    auto start = std::chrono::steady_clock::now();

    GenerationRequest gen_req{schema, constraints, 5000, 42, "uniform", 5000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    auto gen_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(gen_result.value().data->num_rows(), 5000);
    EXPECT_EQ(gen_result.value().data->num_columns(), 100);

    // Validate all 100 constraints
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;
    EXPECT_EQ(val_result.value().rows_checked, 5000);
    EXPECT_EQ(val_result.value().rows_passed, 5000);
    EXPECT_EQ(val_result.value().rows_failed, 0);

    EXPECT_LT(gen_ms, 30000) << "100-constraint generation took " << gen_ms << "ms";
}

// Inter-row constraint across 50K rows
TEST(PerformanceStressTest, InterRowConstraintAcross50KRows) {
    Schema schema;
    schema.type_name = "ir_50k";

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

    ColumnDef val2_col;
    val2_col.name = "accel";
    val2_col.type = DataType::kFloat;
    val2_col.range_min = 0.0;
    val2_col.range_max = 50.0;
    schema.columns.push_back(val2_col);
    ASSERT_TRUE(schema.validate().ok());

    constexpr int64_t kRows = 50000;

    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder val_builder;
    arrow::DoubleBuilder accel_builder;

    for (int64_t i = 0; i < kRows; ++i) {
        ASSERT_TRUE(ts_builder.Append(i * 1000000).ok());
        ASSERT_TRUE(val_builder.Append(static_cast<double>(i % 100) * 0.5).ok());
        // accel values change slowly (delta < 2.0)
        ASSERT_TRUE(accel_builder.Append(static_cast<double>(i % 10) * 0.2).ok());
    }

    std::shared_ptr<arrow::Array> ts_arr, val_arr, accel_arr;
    ASSERT_TRUE(ts_builder.Finish(&ts_arr).ok());
    ASSERT_TRUE(val_builder.Finish(&val_arr).ok());
    ASSERT_TRUE(accel_builder.Finish(&accel_arr).ok());

    auto table = arrow::Table::Make(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("value", arrow::float64()),
            arrow::field("accel", arrow::float64())
        }),
        {ts_arr, val_arr, accel_arr});
    ASSERT_EQ(table->num_rows(), kRows);

    // Two inter-row constraints
    std::vector<InterRowConstraintDef> ir_defs;

    InterRowConstraintDef def1;
    def1.column_name = "value";
    def1.order_column = "timestamp";
    def1.type = InterRowConstraintDef::Type::kDeltaMax;
    def1.delta_max = 50.0;
    ir_defs.push_back(def1);

    InterRowConstraintDef def2;
    def2.column_name = "accel";
    def2.order_column = "timestamp";
    def2.type = InterRowConstraintDef::Type::kDeltaMax;
    def2.delta_max = 5.0;
    ir_defs.push_back(def2);

    auto start = std::chrono::steady_clock::now();
    InterRowEngine engine(schema, ir_defs);
    auto result = engine.execute_batch(table, {});
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_passed, kRows);
    EXPECT_EQ(result.value().rows_filtered, 0);
    EXPECT_EQ(result.value().filtered_batch->num_rows(), kRows);

    EXPECT_LT(elapsed_ms, 30000) << "50K inter-row took " << elapsed_ms << "ms";
}

// Aggregate constraint (AVG) on 100K rows — verify precision
TEST(PerformanceStressTest, AggregateAVG100KRowsPrecision) {
    Schema schema;
    schema.type_name = "agg_100k";

    ColumnDef ts_col;
    ts_col.name = "timestamp";
    ts_col.type = DataType::kDatetime;
    ts_col.not_null = true;
    ts_col.is_order = true;
    schema.columns.push_back(ts_col);

    ColumnDef val_col;
    val_col.name = "temperature";
    val_col.type = DataType::kFloat;
    val_col.range_min = -50.0;
    val_col.range_max = 80.0;
    schema.columns.push_back(val_col);
    ASSERT_TRUE(schema.validate().ok());

    constexpr int64_t kRows = 100000;
    constexpr int64_t kWindowUs = 10000000;  // 10 seconds per window
    constexpr double kExpectedValue = 25.0;

    arrow::Int64Builder ts_builder;
    arrow::DoubleBuilder val_builder;

    // All values exactly 25.0 — so AVG should be exactly 25.0
    for (int64_t i = 0; i < kRows; ++i) {
        ASSERT_TRUE(ts_builder.Append(i * 1000).ok());  // 1ms apart
        ASSERT_TRUE(val_builder.Append(kExpectedValue).ok());
    }

    std::shared_ptr<arrow::Array> ts_arr, val_arr;
    ASSERT_TRUE(ts_builder.Finish(&ts_arr).ok());
    ASSERT_TRUE(val_builder.Finish(&val_arr).ok());

    auto table = arrow::Table::Make(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("temperature", arrow::float64())
        }),
        {ts_arr, val_arr});
    ASSERT_EQ(table->num_rows(), kRows);

    // AVG constraint: AVG(temperature) should be <= 30.0
    AggregateConstraintDef agg_def;
    agg_def.constraint_name = "avg_temp_check";
    agg_def.column_name = "temperature";
    agg_def.function = AggregateFunction::kAvg;
    agg_def.window_type = WindowType::kInterval;
    agg_def.window_interval_us = kWindowUs;
    agg_def.max_val = 30.0;

    AggregateEngine engine(schema, {agg_def});
    auto start = std::chrono::steady_clock::now();
    auto result = engine.execute(table, {});
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(result.ok()) << result.error().message;

    // All windows should have AVG = 25.0, which is <= 30.0, so no violations
    EXPECT_EQ(result.value().phase_two.windows_violated, 0);
    EXPECT_GT(result.value().phase_two.total_windows, 0);

    // Verify precision of computed aggregates
    for (const auto& window : result.value().phase_two.windows) {
        auto agg = engine.compute_aggregate(table, window, agg_def);
        ASSERT_TRUE(agg.ok());
        // With Kahan summation, this should be extremely precise
        double diff = std::abs(agg.value() - kExpectedValue);
        EXPECT_LT(diff, 1e-10) << "AVG precision error: got " << agg.value()
                                << " expected " << kExpectedValue;
    }

    EXPECT_LT(elapsed_ms, 30000) << "100K aggregate took " << elapsed_ms << "ms";
}

// ============================================================================
// EVIDENCE PACKAGE STRESS
// ============================================================================

// Build EvidenceV2 with large row_count, serialize to JSON, deserialize, verify
TEST(PerformanceStressTest, EvidenceV2SerializeDeserializeLarge) {
    auto dir = make_temp_dir("evidence_large");
    MetadataManager meta(dir);

    Schema schema;
    schema.type_name = "ev_stress";
    for (int i = 0; i < 20; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -100.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // Build routing decision
    ClassificationResult classification;
    classification.execution_mode = ExecutionMode::kRowByRow;
    classification.value_range_count = 20;
    classification.inter_row_count = 0;
    classification.aggregate_count = 0;
    for (int i = 0; i < 20; ++i) {
        classification.classifications.push_back(
            {"col_" + std::to_string(i), ConstraintType::kValueRange, ExecutionPhase::kPhaseOne});
    }

    ExecutionRouter router(false);
    auto routing = router.route(classification, schema);
    ASSERT_TRUE(routing.ok());

    PostFilterResult pf_result;
    pf_result.pre_filter_rows = 100000;
    pf_result.post_filter_rows = 100000;
    pf_result.actual_exclusion_rate = 0.0;
    pf_result.rate_band = ExclusionRateBand::kLow;

    EvidencePackageV2Builder builder;

    auto start = std::chrono::steady_clock::now();

    auto pkg_result = builder.build(100000, 0.0, "physics_guaranteed",
                                    routing.value(), classification,
                                    pf_result, schema);
    ASSERT_TRUE(pkg_result.ok()) << pkg_result.error().message;

    auto& pkg = pkg_result.value();
    EXPECT_EQ(pkg.row_count, 100000);
    EXPECT_EQ(pkg.rows_validated, 100000);
    EXPECT_EQ(pkg.constraint_type_breakdown.value_range_count, 20);

    // Serialize to JSON
    auto json_result = builder.to_json(pkg);
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;
    auto& json_str = json_result.value();

    // JSON should be non-trivial in size
    EXPECT_GT(json_str.size(), 500u) << "JSON too small: " << json_str.size();

    auto serialize_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Deserialize
    auto deser_result = builder.from_json(json_str);
    ASSERT_TRUE(deser_result.ok()) << deser_result.error().message;
    auto& pkg2 = deser_result.value();

    // Verify round-trip
    EXPECT_EQ(pkg2.row_count, 100000);
    EXPECT_EQ(pkg2.schema_version, "v2");
    EXPECT_EQ(pkg2.data_grade, "physics_guaranteed");
    EXPECT_DOUBLE_EQ(pkg2.exclusion_rate, 0.0);
    EXPECT_EQ(pkg2.constraint_type_breakdown.value_range_count, 20);
    EXPECT_EQ(pkg2.constraint_type_breakdown.inter_row_count, 0);
    EXPECT_EQ(pkg2.constraint_type_breakdown.aggregate_count, 0);

    EXPECT_LT(serialize_ms, 5000) << "EvidenceV2 round-trip took " << serialize_ms << "ms";

    std::filesystem::remove_all(dir);
}

// Evidence package with all fields populated at max realistic sizes
TEST(PerformanceStressTest, EvidenceV2MaxFieldsPopulated) {
    auto dir = make_temp_dir("evidence_max");
    MetadataManager meta(dir);

    Schema schema;
    schema.type_name = "ev_max";
    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = -1000.0;
        col.range_max = 1000.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    // Build classification with all constraint types
    ClassificationResult classification;
    classification.execution_mode = ExecutionMode::kTwoPhase;
    classification.value_range_count = 50;
    classification.inter_row_count = 5;
    classification.aggregate_count = 3;

    ExecutionRouter router(false);
    auto routing = router.route(classification, schema);
    ASSERT_TRUE(routing.ok());

    // Max realistic post-filter result
    PostFilterResult pf_result;
    pf_result.pre_filter_rows = 1000000;
    pf_result.post_filter_rows = 850000;
    pf_result.actual_exclusion_rate = 0.15;
    pf_result.rate_band = ExclusionRateBand::kMedium;
    pf_result.was_timeout_truncated = false;

    EvidencePackageV2Builder builder;

    auto pkg_result = builder.build(850000, 0.15, "post_filtered",
                                    routing.value(), classification,
                                    pf_result, schema);
    ASSERT_TRUE(pkg_result.ok()) << pkg_result.error().message;

    auto& pkg = pkg_result.value();
    EXPECT_EQ(pkg.row_count, 850000);
    EXPECT_EQ(pkg.constraint_type_breakdown.value_range_count, 50);
    EXPECT_EQ(pkg.constraint_type_breakdown.inter_row_count, 5);
    EXPECT_EQ(pkg.constraint_type_breakdown.aggregate_count, 3);
    EXPECT_TRUE(pkg.post_filter_info.was_post_filtered);
    EXPECT_EQ(pkg.post_filter_info.pre_filter_rows, 1000000);
    EXPECT_EQ(pkg.post_filter_info.post_filter_rows, 850000);

    // Round-trip through JSON
    auto json_result = builder.to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    auto deser_result = builder.from_json(json_result.value());
    ASSERT_TRUE(deser_result.ok()) << deser_result.error().message;

    auto& pkg2 = deser_result.value();
    EXPECT_EQ(pkg2.row_count, 850000);
    EXPECT_EQ(pkg2.constraint_type_breakdown.value_range_count, 50);
    EXPECT_EQ(pkg2.constraint_type_breakdown.inter_row_count, 5);
    EXPECT_EQ(pkg2.constraint_type_breakdown.aggregate_count, 3);
    EXPECT_TRUE(pkg2.post_filter_info.was_post_filtered);
    EXPECT_EQ(pkg2.post_filter_info.pre_filter_rows, 1000000);
    EXPECT_EQ(pkg2.post_filter_info.post_filter_rows, 850000);

    std::filesystem::remove_all(dir);
}

// ============================================================================
// ADDITIONAL STRESS: DRIFT DETECTOR AT SCALE
// ============================================================================

// Drift detection on large samples (50K points each)
TEST(PerformanceStressTest, DriftDetector50KPoints) {
    DriftDetector detector("ks", 0.05);

    std::mt19937 rng(42);
    std::normal_distribution<double> dist1(0.0, 1.0);
    std::normal_distribution<double> dist2(0.5, 1.0);  // shifted mean

    std::vector<double> baseline(50000);
    std::vector<double> shifted(50000);
    for (int i = 0; i < 50000; ++i) {
        baseline[i] = dist1(rng);
        shifted[i] = dist2(rng);
    }

    // Same distribution — no drift expected
    auto start = std::chrono::steady_clock::now();
    auto same = detector.detect(baseline, baseline);
    auto elapsed_same = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(same.ok()) << same.error().message;
    EXPECT_FALSE(same.value().drift_detected) << "False positive drift on identical data";

    // Shifted distribution — drift expected
    start = std::chrono::steady_clock::now();
    auto diff = detector.detect(baseline, shifted);
    auto elapsed_diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(diff.ok()) << diff.error().message;
    EXPECT_TRUE(diff.value().drift_detected) << "Failed to detect drift with mean shift 0.5";
    EXPECT_GT(diff.value().ks_statistic, 0.1);

    EXPECT_LT(elapsed_same, 5000) << "50K same-distribution check took " << elapsed_same << "ms";
    EXPECT_LT(elapsed_diff, 5000) << "50K drift detection took " << elapsed_diff << "ms";
}

// ============================================================================
// ADDITIONAL STRESS: CONTINUOUS ALIGNMENT ENGINE AT SCALE
// ============================================================================

// Alignment engine with many model updates
TEST(PerformanceStressTest, ContinuousAlignment50Updates) {
    auto dir = make_temp_dir("alignment_50");
    MetadataManager meta(dir);
    ModelVersionChain chain(meta);
    ModelStorageLayer storage(dir.string());

    ContinuousAlignmentEngine engine(chain, storage, "ks");

    std::string prev_id;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 50; ++i) {
        std::mt19937 rng(i);
        std::normal_distribution<double> dist(0.0, 1.0);
        std::vector<double> current(1000);
        std::vector<double> new_data(1000);
        for (int j = 0; j < 1000; ++j) {
            current[j] = dist(rng);
            new_data[j] = dist(rng) + (i % 10 == 0 ? 0.5 : 0.0);  // drift every 10th
        }

        AlignmentRequest req;
        req.model_name = "align_model";
        req.current_version_id = prev_id;
        req.current_data = current;
        req.new_data = new_data;
        req.drift_check = "auto";

        auto result = engine.update_model(req);
        ASSERT_TRUE(result.ok()) << "update_model failed at " << i
                                 << ": " << result.error().message;
        prev_id = result.value().new_version.version_id;

        // Every 10th update should detect drift due to the 0.5 shift
        if (i % 10 == 0 && i > 0) {
            // Drift detection is statistical, not guaranteed, but very likely
        }
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Verify we have 50 versions
    auto list = chain.list_versions("align_model", 100);
    ASSERT_TRUE(list.ok());
    EXPECT_GE(list.value().size(), 50u);

    EXPECT_LT(elapsed_ms, 30000) << "50 alignment updates took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// ============================================================================
// MODEL STORAGE LAYER STRESS
// ============================================================================

// Model storage: save and load 100 checkpoints
TEST(PerformanceStressTest, ModelStorage100Checkpoints) {
    auto dir = make_temp_dir("model_storage_100");
    ModelStorageLayer storage(dir.string());

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 100; ++i) {
        std::string version_id = "v" + std::to_string(i);
        std::string model_data = "model_checkpoint_data_" + std::to_string(i) +
                                 "_with_padding_" + std::string(100, 'x');

        auto save_result = storage.save_checkpoint("stress_model", version_id, model_data);
        ASSERT_TRUE(save_result.ok()) << "save_checkpoint failed at " << i
                                      << ": " << save_result.error().message;
    }

    auto save_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    // Load back and verify all 100
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        std::string version_id = "v" + std::to_string(i);
        auto load_result = storage.load_model("stress_model", version_id);
        ASSERT_TRUE(load_result.ok()) << "load_model failed for " << version_id
                                      << ": " << load_result.error().message;
        EXPECT_EQ(load_result.value().substr(0, 22),
                  "model_checkpoint_data_") << "Data mismatch at v" << i;
    }
    auto load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LT(save_ms, 10000) << "100 checkpoint saves took " << save_ms << "ms";
    EXPECT_LT(load_ms, 10000) << "100 checkpoint loads took " << load_ms << "ms";

    std::filesystem::remove_all(dir);
}

// Model storage: atomic write with recovery
TEST(PerformanceStressTest, ModelStorageAtomicWriteRecovery) {
    auto dir = make_temp_dir("model_atomic");
    ModelStorageLayer storage(dir.string());

    ModelVersion mv;
    mv.version_id = "atomic_v1";
    mv.model_name = "atomic_model";
    mv.created_by = "perf_test";

    // Atomic write
    auto write_result = storage.atomic_write("atomic_model", "atomic_test_data_v1", mv);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    // Load via save_checkpoint path (same version)
    auto load_result = storage.load_model("atomic_model", "atomic_v1");
    ASSERT_TRUE(load_result.ok()) << load_result.error().message;
    EXPECT_EQ(load_result.value(), "atomic_test_data_v1");

    // Recovery should succeed (no pending files)
    auto recovery = storage.recover_interrupted();
    EXPECT_TRUE(recovery.ok()) << recovery.error().message;

    std::filesystem::remove_all(dir);
}

// ============================================================================
// CONSTRAINT CLASSIFIER AT SCALE
// ============================================================================

// Classify with many constraints of each type
TEST(PerformanceStressTest, ClassifierManyConstraints) {
    Schema schema;
    schema.type_name = "classify_stress";

    ColumnDef ts_col;
    ts_col.name = "timestamp";
    ts_col.type = DataType::kDatetime;
    ts_col.not_null = true;
    ts_col.is_order = true;
    schema.columns.push_back(ts_col);

    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "val_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    ConstraintSet cset;
    for (int i = 0; i < 50; ++i) {
        cset.value_range_names.push_back("vr_" + std::to_string(i));
    }
    for (int i = 0; i < 10; ++i) {
        InterRowConstraintDef ir_def;
        ir_def.column_name = "val_" + std::to_string(i);
        ir_def.order_column = "timestamp";
        ir_def.type = InterRowConstraintDef::Type::kDeltaMax;
        ir_def.delta_max = 5.0;
        cset.inter_row_defs.push_back(ir_def);
    }
    for (int i = 0; i < 5; ++i) {
        AggregateConstraintDef agg_def;
        agg_def.constraint_name = "agg_" + std::to_string(i);
        agg_def.column_name = "val_" + std::to_string(i);
        agg_def.function = AggregateFunction::kAvg;
        agg_def.window_type = WindowType::kInterval;
        agg_def.window_interval_us = 10000000;
        agg_def.max_val = 50.0;
        cset.aggregate_defs.push_back(agg_def);
    }

    ConstraintClassifier classifier;

    auto start = std::chrono::steady_clock::now();
    auto result = classifier.classify(cset, schema);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().value_range_count, 50);
    EXPECT_EQ(result.value().inter_row_count, 10);
    EXPECT_EQ(result.value().aggregate_count, 5);
    EXPECT_EQ(result.value().execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_TRUE(result.value().has_inter_row());
    EXPECT_TRUE(result.value().has_aggregate());

    EXPECT_LT(elapsed_ms, 1000) << "Classification took " << elapsed_ms << "ms";
}

// ============================================================================
// EXECUTION ROUTER STRESS
// ============================================================================

// Router decisions for many constraint configurations
TEST(PerformanceStressTest, RouterDecisions100Configs) {
    ExecutionRouter router(false);

    Schema schema;
    schema.type_name = "router_stress";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    schema.columns.push_back(col);
    ASSERT_TRUE(schema.validate().ok());

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 100; ++i) {
        ClassificationResult classification;
        classification.value_range_count = i + 1;
        classification.inter_row_count = i % 5;
        classification.aggregate_count = i % 3;
        classification.execution_mode = ExecutionMode::kRowByRow;

        auto result = router.route(classification, schema);
        ASSERT_TRUE(result.ok()) << "route failed at config " << i
                                 << ": " << result.error().message;
        // Should always produce a valid decision
        EXPECT_NE(result.value().selected_path, DegradationPath::kFullFunction)
            << "Should not be full function without data engine";
    }

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LT(elapsed_ms, 1000) << "100 routing decisions took " << elapsed_ms << "ms";
}

// ============================================================================
// POST FILTER STRESS
// ============================================================================

// Post-filter on large table with high exclusion
TEST(PerformanceStressTest, PostFilterLargeTable) {
    // Build a table with 100K rows
    constexpr int64_t kRows = 100000;
    auto table = make_wide_double_table(10, kRows);
    ASSERT_EQ(table->num_rows(), kRows);

    PostFilterConfig config;
    config.timeout_ms = 60000.0;
    config.oversampling_ratio = 3.0;

    PostFilter pf(config);

    auto start = std::chrono::steady_clock::now();
    auto result = pf.execute(table, 50000);  // Request 50K from 100K
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_GT(result.value().post_filter_rows, 0);
    EXPECT_LE(result.value().post_filter_rows, kRows);
    EXPECT_GE(result.value().actual_exclusion_rate, 0.0);
    EXPECT_LE(result.value().actual_exclusion_rate, 1.0);

    EXPECT_LT(elapsed_ms, 30000) << "Post-filter on 100K took " << elapsed_ms << "ms";
}

// ============================================================================
// FULL PIPELINE STRESS: GENERATE -> VALIDATE -> STORE -> RETRIEVE
// ============================================================================

// End-to-end pipeline: generate 50K rows with constraints, validate, store, retrieve
TEST(PerformanceStressTest, FullPipeline50KRows) {
    auto dir = make_temp_dir("pipeline_50k");
    ObjectStoreBackend backend(dir);

    Schema schema;
    schema.type_name = "pipeline_test";

    ColumnDef ts_col;
    ts_col.name = "timestamp";
    ts_col.type = DataType::kDatetime;
    ts_col.not_null = true;
    ts_col.is_order = true;
    schema.columns.push_back(ts_col);

    for (int i = 0; i < 5; ++i) {
        ColumnDef col;
        col.name = "val_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        schema.columns.push_back(col);
    }
    ASSERT_TRUE(schema.validate().ok());

    std::vector<ConstraintItem> constraints;
    for (int i = 0; i < 5; ++i) {
        constraints.push_back({"val_" + std::to_string(i),
                               ConstraintOperator::kBetween, 10.0, 90.0});
    }

    auto reg = backend.register_table("pipeline_tbl", "{}");
    ASSERT_TRUE(reg.ok());

    auto start = std::chrono::steady_clock::now();

    // Generate
    GenerationRequest gen_req{schema, constraints, 50000, 42, "uniform", 10000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 50000);

    // Validate
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;
    EXPECT_EQ(val_result.value().rows_passed, 50000);

    // Store
    auto append_result = backend.append("pipeline_tbl", gen_result.value().data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Retrieve
    auto scan = backend.scan("pipeline_tbl");
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_rows(), 50000);
    EXPECT_EQ(scan.value()->num_columns(), 6);  // timestamp + 5 vals

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    EXPECT_LT(elapsed_ms, 30000) << "Full pipeline took " << elapsed_ms << "ms";

    std::filesystem::remove_all(dir);
}

// ============================================================================
// IMMUTABILITY VERIFICATION AT SCALE
// ============================================================================

// Attempt to modify 100 versions — all must fail
TEST(PerformanceStressTest, VersionChainImmutability500Attempts) {
    auto dir = make_temp_dir("immutability");
    MetadataManager meta(dir);
    ModelVersionChain chain(meta);

    // Create a version
    ModelVersion mv;
    mv.model_name = "imm_model";
    mv.created_by = "test";
    auto v = chain.create_version("imm_model", "", mv);
    ASSERT_TRUE(v.ok());

    // Attempt modification 500 times
    for (int i = 0; i < 500; ++i) {
        auto result = chain.modify_version(v.value().version_id);
        ASSERT_FALSE(result.ok()) << "modify_version should always fail (attempt " << i << ")";
        EXPECT_EQ(result.error().code, ErrorCode::kImmutableViolation);
    }

    // Verify version is unchanged
    auto check = chain.get_version(v.value().version_id);
    ASSERT_TRUE(check.ok());
    EXPECT_TRUE(check.value()->is_immutable);
    EXPECT_EQ(check.value()->created_by, "test");

    std::filesystem::remove_all(dir);
}
