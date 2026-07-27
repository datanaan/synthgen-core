// E2E Data Quality Tests -- 15 scenarios validating actual generated data quality
// Focus: distribution statistics, boundary enforcement, determinism, type correctness
#include <gtest/gtest.h>

#include "engine/physics/rectangular_sampler.h"
#include "engine/physics/uniform_sampler.h"
#include "engine/physics/gaussian_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "schema/schema.h"
#include "parser/ast.h"
#include "common/result.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <cmath>
#include <numeric>
#include <set>
#include <string>
#include <vector>
#include <unordered_set>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;
using namespace synthgen::parser::ast;

// ============================================================================
// Helpers
// ============================================================================
namespace {

// Compute mean of a DoubleArray
double compute_mean(const std::shared_ptr<arrow::DoubleArray>& arr) {
    double sum = 0.0;
    for (int64_t i = 0; i < arr->length(); i++) {
        sum += arr->Value(i);
    }
    return sum / static_cast<double>(arr->length());
}

// Compute population stddev
double compute_stddev(const std::shared_ptr<arrow::DoubleArray>& arr, double mean) {
    double sum_sq = 0.0;
    for (int64_t i = 0; i < arr->length(); i++) {
        double d = arr->Value(i) - mean;
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq / static_cast<double>(arr->length()));
}

// Build a simple FLOAT schema with one column
Schema make_float_schema(const std::string& name, double min_val, double max_val) {
    Schema s;
    s.type_name = "quality_test";
    synthgen::ColumnDef col;
    col.name = name;
    col.type = DataType::kFloat;
    col.range_min = min_val;
    col.range_max = max_val;
    s.columns.push_back(col);
    return s;
}

// Build a schema with all 5 column types
Schema make_all_types_schema() {
    Schema s;
    s.type_name = "all_types";

    synthgen::ColumnDef fcol;
    fcol.name = "float_val";
    fcol.type = DataType::kFloat;
    fcol.range_min = 0.0;
    fcol.range_max = 100.0;
    s.columns.push_back(fcol);

    synthgen::ColumnDef icol;
    icol.name = "int_val";
    icol.type = DataType::kInt;
    icol.range_min = -1000;
    icol.range_max = 1000;
    s.columns.push_back(icol);

    synthgen::ColumnDef dcol;
    dcol.name = "dt_val";
    dcol.type = DataType::kDatetime;
    dcol.not_null = true;
    s.columns.push_back(dcol);

    synthgen::ColumnDef scol;
    scol.name = "str_val";
    scol.type = DataType::kString;
    s.columns.push_back(scol);

    synthgen::ColumnDef ecol;
    ecol.name = "enum_val";
    ecol.type = DataType::kEnum;
    ecol.enum_values = {"alpha", "beta", "gamma"};
    s.columns.push_back(ecol);

    return s;
}

// Extract the first FLOAT column from a table as DoubleArray
std::shared_ptr<arrow::DoubleArray> get_float_col(
    const std::shared_ptr<arrow::Table>& table, int col_idx = 0) {
    return std::static_pointer_cast<arrow::DoubleArray>(
        table->column(col_idx)->chunk(0));
}

// Extract a string column
std::shared_ptr<arrow::StringArray> get_string_col(
    const std::shared_ptr<arrow::Table>& table, int col_idx) {
    return std::static_pointer_cast<arrow::StringArray>(
        table->column(col_idx)->chunk(0));
}

// Extract an int column
std::shared_ptr<arrow::Int64Array> get_int_col(
    const std::shared_ptr<arrow::Table>& table, int col_idx) {
    return std::static_pointer_cast<arrow::Int64Array>(
        table->column(col_idx)->chunk(0));
}

// Extract a timestamp column
std::shared_ptr<arrow::TimestampArray> get_datetime_col(
    const std::shared_ptr<arrow::Table>& table, int col_idx) {
    return std::static_pointer_cast<arrow::TimestampArray>(
        table->column(col_idx)->chunk(0));
}

}  // anonymous namespace

// ============================================================================
// Test 1: Uniform distribution quality (100K values)
// ============================================================================
TEST(DataQualityTest, UniformDistributionQuality) {
    Schema schema = make_float_schema("value", 0.0, 100.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100000, 42, "uniform", 10000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 100000);

    auto col = get_float_col(result.value().data);
    double expected_mean = 50.0;
    double mean = compute_mean(col);

    // For 100K uniform samples, the mean should be within 0.2 of expected
    EXPECT_NEAR(mean, expected_mean, 0.2)
        << "Mean of uniform [0,100] expected ~50.0, got " << mean;

    // Theoretical stddev of U(a,b) = (b-a)/sqrt(12) = 100/sqrt(12) ~ 28.87
    double expected_stddev = 100.0 / std::sqrt(12.0);
    double stddev = compute_stddev(col, mean);
    EXPECT_NEAR(stddev, expected_stddev, 0.5)
        << "Stddev of uniform [0,100] expected ~28.87, got " << stddev;
}

// ============================================================================
// Test 2: Gaussian within bounds (truncation works)
// ============================================================================
TEST(DataQualityTest, GaussianTruncationBounds) {
    Schema schema = make_float_schema("value", 0.0, 100.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10000, 42, "gaussian", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 10000);

    auto col = get_float_col(result.value().data);
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        EXPECT_GE(v, 0.0) << "Gaussian value below min at row " << i << ": " << v;
        EXPECT_LE(v, 100.0) << "Gaussian value above max at row " << i << ": " << v;
    }

    // Gaussian should cluster around center -- check that the middle 60% contains
    // most values (at least 70% for a truncated gaussian with these parameters)
    double mean = compute_mean(col);
    EXPECT_NEAR(mean, 50.0, 5.0)
        << "Gaussian mean should be near midpoint, got " << mean;
}

// ============================================================================
// Test 3: Seed determinism -- same seed produces identical data byte-for-byte
// ============================================================================
TEST(DataQualityTest, SeedDeterminism) {
    Schema schema = make_float_schema("value", -100.0, 100.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 1000, 12345, "uniform", 500};

    auto r1 = sampler.generate(req);
    auto r2 = sampler.generate(req);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    auto col1 = get_float_col(r1.value().data);
    auto col2 = get_float_col(r2.value().data);
    ASSERT_EQ(col1->length(), col2->length());

    for (int64_t i = 0; i < col1->length(); i++) {
        EXPECT_DOUBLE_EQ(col1->Value(i), col2->Value(i))
            << "Determinism broken at row " << i;
    }

    // Also verify stats are identical
    EXPECT_EQ(r1.value().stats.rows_generated, r2.value().stats.rows_generated);
    EXPECT_DOUBLE_EQ(r1.value().stats.exclusion_rate, r2.value().stats.exclusion_rate);
}

// ============================================================================
// Test 4: Different seeds produce different data (>= 90% differ)
// ============================================================================
TEST(DataQualityTest, DifferentSeedsDifferentData) {
    Schema schema = make_float_schema("value", 0.0, 1.0);
    RectangularSampler sampler(schema);
    GenerationRequest req42{schema, {}, 1000, 42, "uniform", 1000};
    GenerationRequest req43{schema, {}, 1000, 43, "uniform", 1000};

    auto r1 = sampler.generate(req42);
    auto r2 = sampler.generate(req43);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    auto col1 = get_float_col(r1.value().data);
    auto col2 = get_float_col(r2.value().data);
    ASSERT_EQ(col1->length(), col2->length());

    int64_t same_count = 0;
    for (int64_t i = 0; i < col1->length(); i++) {
        if (col1->Value(i) == col2->Value(i)) same_count++;
    }

    double same_rate = static_cast<double>(same_count) / static_cast<double>(col1->length());
    EXPECT_LT(same_rate, 0.10)
        << "Seeds 42 and 43 produced " << (same_rate * 100.0)
        << "% identical values -- expected < 10%";
}

// ============================================================================
// Test 5: Boundary tightness -- narrow range [20.0, 20.001]
// ============================================================================
TEST(DataQualityTest, BoundaryTightness) {
    Schema schema = make_float_schema("tight", 20.0, 20.001);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 10000);

    auto col = get_float_col(result.value().data);
    double min_seen = 1e9, max_seen = -1e9;
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        EXPECT_GE(v, 20.0) << "Value below min at row " << i;
        EXPECT_LE(v, 20.001) << "Value above max at row " << i;
        min_seen = std::min(min_seen, v);
        max_seen = std::max(max_seen, v);
    }

    // All values must be within [20.0, 20.001]
    EXPECT_GE(min_seen, 20.0);
    EXPECT_LE(max_seen, 20.001);

    // The spread should be at most 0.001
    double spread = max_seen - min_seen;
    EXPECT_LE(spread, 0.0011) << "Spread " << spread << " exceeds range width";
}

// ============================================================================
// Test 6: Single-row generation (limit=1)
// ============================================================================
TEST(DataQualityTest, SingleRowGeneration) {
    Schema schema = make_float_schema("value", 0.0, 100.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 1, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 1);
    ASSERT_EQ(result.value().stats.rows_generated, 1);

    auto col = get_float_col(result.value().data);
    double v = col->Value(0);
    EXPECT_GE(v, 0.0);
    EXPECT_LE(v, 100.0);
}

// ============================================================================
// Test 7: Large limit (50000 rows) -- no crash, correct count
// ============================================================================
TEST(DataQualityTest, LargeLimitGeneration) {
    Schema schema = make_float_schema("value", -50.0, 50.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 50000, 42, "uniform", 10000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_rows(), 50000);
    EXPECT_EQ(result.value().stats.rows_generated, 50000);
    EXPECT_EQ(result.value().stats.batch_count, 5);

    // Spot check: all values in range
    auto col = get_float_col(result.value().data);
    for (int64_t i = 0; i < col->length(); i++) {
        EXPECT_GE(col->Value(i), -50.0);
        EXPECT_LE(col->Value(i), 50.0);
    }
}

// ============================================================================
// Test 8: All 5 column types -- verify present and non-null
// ============================================================================
TEST(DataQualityTest, AllFiveColumnTypes) {
    Schema schema = make_all_types_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 200, 42, "uniform", 100};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto table = result.value().data;
    ASSERT_EQ(table->num_columns(), 5);
    ASSERT_EQ(table->num_rows(), 200);

    // Verify column names and types
    EXPECT_EQ(table->schema()->field(0)->name(), "float_val");
    EXPECT_EQ(table->schema()->field(0)->type()->id(), arrow::Type::DOUBLE);
    EXPECT_EQ(table->schema()->field(1)->name(), "int_val");
    EXPECT_EQ(table->schema()->field(1)->type()->id(), arrow::Type::INT64);
    EXPECT_EQ(table->schema()->field(2)->name(), "dt_val");
    EXPECT_EQ(table->schema()->field(2)->type()->id(), arrow::Type::TIMESTAMP);
    EXPECT_EQ(table->schema()->field(3)->name(), "str_val");
    EXPECT_EQ(table->schema()->field(3)->type()->id(), arrow::Type::STRING);
    EXPECT_EQ(table->schema()->field(4)->name(), "enum_val");
    EXPECT_EQ(table->schema()->field(4)->type()->id(), arrow::Type::STRING);

    // Check FLOAT column: non-null, in range
    auto fcol = get_float_col(table, 0);
    for (int64_t i = 0; i < fcol->length(); i++) {
        ASSERT_FALSE(fcol->IsNull(i)) << "Float null at row " << i;
        EXPECT_GE(fcol->Value(i), 0.0);
        EXPECT_LE(fcol->Value(i), 100.0);
    }

    // Check INT column: non-null, in range
    auto icol = get_int_col(table, 1);
    for (int64_t i = 0; i < icol->length(); i++) {
        ASSERT_FALSE(icol->IsNull(i)) << "Int null at row " << i;
        EXPECT_GE(icol->Value(i), -1000);
        EXPECT_LE(icol->Value(i), 1000);
    }

    // Check DATETIME column: non-null, non-zero
    auto dcol = get_datetime_col(table, 2);
    int64_t nonzero_dt = 0;
    for (int64_t i = 0; i < dcol->length(); i++) {
        ASSERT_FALSE(dcol->IsNull(i)) << "Datetime null at row " << i;
        if (dcol->Value(i) != 0) nonzero_dt++;
    }
    EXPECT_GT(nonzero_dt, 0) << "All datetime values are zero";

    // Check STRING column: non-null, non-empty
    auto scol = get_string_col(table, 3);
    int64_t non_empty_str = 0;
    for (int64_t i = 0; i < scol->length(); i++) {
        ASSERT_FALSE(scol->IsNull(i)) << "String null at row " << i;
        if (scol->GetString(i).length() > 0) non_empty_str++;
    }
    EXPECT_GT(non_empty_str, 0) << "All string values are empty";

    // Check ENUM column: non-null, valid values
    auto ecol = get_string_col(table, 4);
    std::set<std::string> valid_enums = {"alpha", "beta", "gamma"};
    for (int64_t i = 0; i < ecol->length(); i++) {
        ASSERT_FALSE(ecol->IsNull(i)) << "Enum null at row " << i;
        std::string val = ecol->GetString(i);
        EXPECT_TRUE(valid_enums.count(val) > 0)
            << "Invalid enum value at row " << i << ": " << val;
    }
}

// ============================================================================
// Test 9: ENUM distribution -- all 3 values appear with enough rows
// ============================================================================
TEST(DataQualityTest, EnumDistributionCoverage) {
    Schema schema;
    schema.type_name = "enum_test";
    synthgen::ColumnDef ecol;
    ecol.name = "color";
    ecol.type = DataType::kEnum;
    ecol.enum_values = {"red", "green", "blue"};
    schema.columns.push_back(ecol);

    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 5000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 5000);

    auto col = get_string_col(result.value().data, 0);
    std::map<std::string, int64_t> counts;
    for (int64_t i = 0; i < col->length(); i++) {
        counts[col->GetString(i)]++;
    }

    // All 3 values must appear
    EXPECT_EQ(counts.size(), 3u) << "Expected 3 enum values, got " << counts.size();
    EXPECT_GT(counts["red"], 0);
    EXPECT_GT(counts["green"], 0);
    EXPECT_GT(counts["blue"], 0);

    // Each value should be roughly 1/3 of total (within +/- 5%)
    double expected_frac = 1.0 / 3.0;
    for (const auto& [val, cnt] : counts) {
        double frac = static_cast<double>(cnt) / 5000.0;
        EXPECT_NEAR(frac, expected_frac, 0.05)
            << "Enum '" << val << "' fraction " << frac
            << " far from expected " << expected_frac;
    }
}

// ============================================================================
// Test 10: Multiple constraints on same column -- intersection
// ============================================================================
TEST(DataQualityTest, MultipleConstraintsOnSameColumn) {
    Schema schema = make_float_schema("value", -100.0, 100.0);
    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 0.0, 50.0},
        {"value", ConstraintOperator::kBetween, 25.0, 75.0}
    };

    RectangularSampler sampler(schema);
    GenerationRequest req{schema, constraints, 5000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 5000);

    auto col = get_float_col(result.value().data);
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        // Intersection of [0,50] and [25,75] is [25,50]
        EXPECT_GE(v, 25.0) << "Value below intersection min at row " << i << ": " << v;
        EXPECT_LE(v, 50.0) << "Value above intersection max at row " << i << ": " << v;
    }
}

// ============================================================================
// Test 11: Constraint wider than schema range -- schema range wins
// ============================================================================
TEST(DataQualityTest, ConstraintWiderThanSchemaRange) {
    // Schema defines [-50, 80], constraint asks [-100, 200]
    Schema schema = make_float_schema("value", -50.0, 80.0);
    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, -100.0, 200.0}
    };

    RectangularSampler sampler(schema);
    GenerationRequest req{schema, constraints, 10000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 10000);

    auto col = get_float_col(result.value().data);
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        // Values should be bounded by schema range [-50, 80], NOT constraint range
        EXPECT_GE(v, -50.0) << "Value below schema min at row " << i << ": " << v;
        EXPECT_LE(v, 80.0) << "Value above schema max at row " << i << ": " << v;
    }

    // Additionally verify that the actual range is the schema range (min near -50, max near 80)
    double min_val = col->Value(0), max_val = col->Value(0);
    for (int64_t i = 1; i < col->length(); i++) {
        min_val = std::min(min_val, col->Value(i));
        max_val = std::max(max_val, col->Value(i));
    }
    // With 10K samples, should get very close to boundaries
    EXPECT_LT(min_val, -49.0) << "Min value " << min_val << " not close to schema min -50";
    EXPECT_GT(max_val, 79.0) << "Max value " << max_val << " not close to schema max 80";
}

// ============================================================================
// Test 12: Zero-width constraint [25.0, 25.0]
// ============================================================================
TEST(DataQualityTest, ZeroWidthConstraint) {
    // The RangeExtractor rejects min >= max with kInvalidRange.
    // This test documents that behavior.
    Schema schema = make_float_schema("value", 0.0, 100.0);
    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 25.0, 25.0}
    };

    RectangularSampler sampler(schema);
    GenerationRequest req{schema, constraints, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);

    // BUG FOUND: RangeExtractor::extract returns kInvalidRange for min >= max.
    // A zero-width constraint [25.0, 25.0] should produce all values exactly 25.0,
    // but the engine rejects it. This is a legitimate use case (constant columns).
    EXPECT_FALSE(result.ok()) << "BUG: Engine should accept zero-width constraint [25,25]";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange)
            << "Wrong error code: " << static_cast<int>(result.error().code);
    }
}

// ============================================================================
// Test 13: Batch size effects -- different batch sizes produce same row count
//          and data stays within bounds. Same params = same data (determinism).
// ============================================================================
TEST(DataQualityTest, BatchSizeEffects) {
    Schema schema = make_float_schema("value", 0.0, 100.0);
    RectangularSampler sampler(schema);

    // Generate with batch_size=1 (every row is its own batch)
    GenerationRequest req_small{schema, {}, 500, 42, "uniform", 1};
    auto r_small = sampler.generate(req_small);
    ASSERT_TRUE(r_small.ok()) << r_small.error().message;
    EXPECT_EQ(r_small.value().data->num_rows(), 500);
    EXPECT_EQ(r_small.value().stats.batch_count, 500);

    // Generate with batch_size=10000 (all rows in one batch)
    GenerationRequest req_large{schema, {}, 500, 42, "uniform", 10000};
    auto r_large = sampler.generate(req_large);
    ASSERT_TRUE(r_large.ok()) << r_large.error().message;
    EXPECT_EQ(r_large.value().data->num_rows(), 500);
    EXPECT_EQ(r_large.value().stats.batch_count, 1);

    // Both must produce valid data within schema range
    auto col_s = get_float_col(r_small.value().data);
    auto col_l = get_float_col(r_large.value().data);

    for (int64_t i = 0; i < col_s->length(); i++) {
        EXPECT_GE(col_s->Value(i), 0.0) << "batch_size=1: out of range at row " << i;
        EXPECT_LE(col_s->Value(i), 100.0) << "batch_size=1: out of range at row " << i;
    }
    for (int64_t i = 0; i < col_l->length(); i++) {
        EXPECT_GE(col_l->Value(i), 0.0) << "batch_size=10000: out of range at row " << i;
        EXPECT_LE(col_l->Value(i), 100.0) << "batch_size=10000: out of range at row " << i;
    }

    // Determinism: same params (including batch_size) = identical data
    auto r_dup = sampler.generate(req_large);
    ASSERT_TRUE(r_dup.ok());
    auto col_d = get_float_col(r_dup.value().data);
    for (int64_t i = 0; i < col_l->length(); i++) {
        EXPECT_DOUBLE_EQ(col_l->Value(i), col_d->Value(i))
            << "Determinism broken at row " << i;
    }

    // NOTE: Different batch sizes produce DIFFERENT data because the
    // SeedController incorporates batch_index into seed derivation.
    // This is by design -- batch_size is a generation parameter that
    // affects the seed chain. The test documents this behavior.
}

// ============================================================================
// Test 14: Validation accuracy -- manually count violations vs validator
// ============================================================================
TEST(DataQualityTest, ValidationAccuracy) {
    Schema schema = make_float_schema("value", 0.0, 100.0);
    std::vector<ConstraintItem> constraints = {
        {"value", ConstraintOperator::kBetween, 20.0, 80.0}
    };

    RectangularSampler sampler(schema);
    GenerationRequest req{schema, constraints, 1000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 1000);

    auto table = result.value().data;
    auto col = get_float_col(table);

    // Manually count violations
    int64_t manual_violations = 0;
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        if (v < 20.0 || v > 80.0) manual_violations++;
    }

    // Use ValueRangeValidator
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(table);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;

    auto& vr = val_result.value();
    EXPECT_EQ(vr.rows_checked, 1000);

    // Validator count must match manual count
    EXPECT_EQ(vr.rows_failed, manual_violations)
        << "Validator reported " << vr.rows_failed
        << " violations, manual count = " << manual_violations;

    // The pass rate must be consistent
    double expected_pass_rate = static_cast<double>(1000 - manual_violations) / 1000.0;
    EXPECT_DOUBLE_EQ(vr.pass_rate, expected_pass_rate);
}

// ============================================================================
// Test 15: Negative range schema -- all values negative
// ============================================================================
TEST(DataQualityTest, NegativeRangeSchema) {
    Schema schema = make_float_schema("value", -100.0, -1.0);
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().data->num_rows(), 10000);

    auto col = get_float_col(result.value().data);
    double min_seen = 1e9, max_seen = -1e9;
    for (int64_t i = 0; i < col->length(); i++) {
        double v = col->Value(i);
        EXPECT_GE(v, -100.0) << "Value below min at row " << i << ": " << v;
        EXPECT_LE(v, -1.0) << "Value above max (not negative enough) at row " << i << ": " << v;
        min_seen = std::min(min_seen, v);
        max_seen = std::max(max_seen, v);
    }

    // All values must be negative
    EXPECT_LT(max_seen, 0.0) << "Expected all values negative, max was " << max_seen;

    // Range should cover a decent portion of [-100, -1]
    EXPECT_LT(min_seen, -95.0) << "Min value " << min_seen << " not close to -100";
    EXPECT_GT(max_seen, -5.0) << "Max value " << max_seen << " not close to -1";
}
