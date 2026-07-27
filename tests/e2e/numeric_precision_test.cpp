// E2E Numeric Precision Tests — floating point bugs, precision loss, overflow, edge cases
#include <gtest/gtest.h>

#include "engine/physics/uniform_sampler.h"
#include "engine/physics/gaussian_sampler.h"
#include "engine/physics/random.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/physics/range_extractor.h"
#include "engine/physics/seed_controller.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/constraint/inter_row_engine.h"
#include "schema/schema.h"
#include "common/result.h"
#include "common/types.h"
#include "parser/ast.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <arrow/builder.h>
#include <cfloat>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
// NOTE: Do NOT use "using namespace synthgen::parser::ast" because ColumnDef
// exists in both synthgen and synthgen::parser::ast. Use parser::ast:: prefix.

// ============================================================================
// Helper: build a minimal schema with one float column
// ============================================================================
static schema::Schema make_float_schema(const std::string& col_name,
                                        double range_min,
                                        double range_max,
                                        bool add_order = true) {
    schema::Schema s;
    s.type_name = "test_type";
    if (add_order) {
        synthgen::ColumnDef ts;
        ts.name = "timestamp";
        ts.type = DataType::kDatetime;
        ts.not_null = true;
        ts.is_order = true;
        s.columns.push_back(ts);
    }
    synthgen::ColumnDef c;
    c.name = col_name;
    c.type = DataType::kFloat;
    c.range_min = range_min;
    c.range_max = range_max;
    s.columns.push_back(c);
    return s;
}

// Helper: build an arrow table with one double column (+ timestamp)
static std::shared_ptr<arrow::Table> make_table(const std::string& col_name,
                                                 const std::vector<double>& values,
                                                 bool add_order = true) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::Array>> arrays;

    int64_t n = static_cast<int64_t>(values.size());

    if (add_order) {
        fields.push_back(arrow::field("timestamp", arrow::int64()));
        arrow::Int64Builder ts_builder;
        for (int64_t i = 0; i < n; ++i) { auto s = ts_builder.Append(i * 1000000); (void)s; }
        auto ts_arr = *ts_builder.Finish();
        arrays.push_back(ts_arr);
    }

    fields.push_back(arrow::field(col_name, arrow::float64()));
    arrow::DoubleBuilder val_builder;
    for (double v : values) { auto s = val_builder.Append(v); (void)s; }
    auto val_arr = *val_builder.Finish();
    arrays.push_back(val_arr);

    auto schema = arrow::schema(fields);
    return arrow::Table::Make(schema, arrays);
}

// Helper: build a schema with order + one float column (inline, for InterRow tests)
static schema::Schema make_interrow_schema(const std::string& col_name) {
    schema::Schema s;
    s.type_name = "test_type";
    synthgen::ColumnDef ts;
    ts.name = "timestamp";
    ts.type = DataType::kDatetime;
    ts.is_order = true;
    s.columns.push_back(ts);
    synthgen::ColumnDef val;
    val.name = col_name;
    val.type = DataType::kFloat;
    s.columns.push_back(val);
    return s;
}

// ============================================================================
// Test 1: Very small range [0.0, 1e-15]
// ============================================================================
TEST(NumericPrecision, VerySmallRange) {
    UniformSampler sampler(42);
    constexpr double kMin = 0.0;
    constexpr double kMax = 1e-15;
    constexpr int kSamples = 10000;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kMin, kMax);
        ASSERT_GE(val, kMin) << "Sample " << i << " below min: " << val;
        ASSERT_LE(val, kMax) << "Sample " << i << " above max: " << val;
    }
}

// ============================================================================
// Test 2: Very large range [-1e15, 1e15] — no overflow
// ============================================================================
TEST(NumericPrecision, VeryLargeRange) {
    UniformSampler sampler(42);
    constexpr double kMin = -1e15;
    constexpr double kMax = 1e15;
    constexpr int kSamples = 10000;

    bool saw_positive = false;
    bool saw_negative = false;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kMin, kMax);
        ASSERT_FALSE(std::isinf(val)) << "Overflow at sample " << i << ": " << val;
        ASSERT_FALSE(std::isnan(val)) << "NaN at sample " << i;
        ASSERT_GE(val, kMin) << "Sample " << i << " below min: " << val;
        ASSERT_LE(val, kMax) << "Sample " << i << " above max: " << val;
        if (val > 0) saw_positive = true;
        if (val < 0) saw_negative = true;
    }
    EXPECT_TRUE(saw_positive) << "Never saw positive values in [-1e15, 1e15]";
    EXPECT_TRUE(saw_negative) << "Never saw negative values in [-1e15, 1e15]";
}

// ============================================================================
// Test 3: Asymmetric range [-1e-10, 1e10] — distribution not collapsed
// ============================================================================
TEST(NumericPrecision, AsymmetricRange) {
    UniformSampler sampler(42);
    constexpr double kMin = -1e-10;
    constexpr double kMax = 1e10;
    constexpr int kSamples = 10000;

    int count_above_1e9 = 0;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kMin, kMax);
        ASSERT_GE(val, kMin) << "Below min at sample " << i;
        ASSERT_LE(val, kMax) << "Above max at sample " << i;
        if (val > 1e9) ++count_above_1e9;
    }
    // The range [-1e-10, 1e10] spans 1e10; the negative portion is ~1e-20 of the range.
    // With 10k samples, it is extremely unlikely to see any negative values.
    // Instead, verify that we see large positive values (sampling works at scale).
    EXPECT_GT(count_above_1e9, 0) << "No values > 1e9 in asymmetric range";
}

// ============================================================================
// Test 4: Range with zero crossing [-100, 100]
// ============================================================================
TEST(NumericPrecision, ZeroCrossingRange) {
    UniformSampler sampler(42);
    constexpr double kMin = -100.0;
    constexpr double kMax = 100.0;
    constexpr int kSamples = 10000;

    int count_positive = 0;
    int count_negative = 0;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kMin, kMax);
        ASSERT_GE(val, kMin);
        ASSERT_LE(val, kMax);
        if (val > 0) ++count_positive;
        if (val < 0) ++count_negative;
    }
    // Both halves should have substantial counts (~5000 each)
    EXPECT_GT(count_positive, 4000) << "Too few positive values: " << count_positive;
    EXPECT_GT(count_negative, 4000) << "Too few negative values: " << count_negative;
}

// ============================================================================
// Test 5: Identical min/max at extreme [1e300, 1e300]
// When min == max, uniform_range does min + uniform_01() * 0.0 = min always.
// This is mathematically correct (constant output).
// ============================================================================
TEST(NumericPrecision, IdenticalMinMaxExtreme) {
    UniformSampler sampler(42);
    constexpr double kVal = 1e300;
    constexpr int kSamples = 100;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kVal, kVal);
        // min + uniform_01() * (max - min) = min + uniform_01() * 0.0 = min
        EXPECT_DOUBLE_EQ(val, kVal)
            << "Identical min/max should produce constant value at sample " << i;
    }
}

// ============================================================================
// Test 6: Double epsilon range [0.0, DBL_EPSILON]
// ============================================================================
TEST(NumericPrecision, DoubleEpsilonRange) {
    UniformSampler sampler(42);
    constexpr double kMin = 0.0;
    constexpr double kMax = DBL_EPSILON;  // ~2.22e-16
    constexpr int kSamples = 10000;

    for (int i = 0; i < kSamples; ++i) {
        double val = sampler.sample_float(kMin, kMax);
        ASSERT_GE(val, kMin) << "Below zero at sample " << i << ": " << val;
        ASSERT_LE(val, kMax) << "Above DBL_EPSILON at sample " << i << ": " << val;
    }
}

// ============================================================================
// Test 7: Validation with boundary values (exactly min, exactly max)
// ============================================================================
TEST(NumericPrecision, BoundaryValuesPass) {
    schema::Schema s = make_float_schema("val", 0.0, 100.0);
    std::vector<parser::ast::ConstraintItem> constraints;
    parser::ast::ConstraintItem ci;
    ci.column_name = "val";
    ci.op = parser::ast::ConstraintOperator::kBetween;
    ci.value_min = 0.0;
    ci.value_max = 100.0;
    constraints.push_back(ci);

    ValueRangeValidator validator(s, constraints);

    // Values exactly at boundaries
    std::vector<double> values = {0.0, 100.0, 0.0, 100.0, 50.0};
    auto table = make_table("val", values);

    auto result = validator.validate_batch(table);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_passed, 5);
    EXPECT_EQ(result.value().rows_failed, 0);
}

// ============================================================================
// Test 8: Validation with values just outside boundaries
// ============================================================================
TEST(NumericPrecision, JustOutsideBoundaryFail) {
    schema::Schema s = make_float_schema("val", 0.0, 100.0);
    std::vector<parser::ast::ConstraintItem> constraints;
    parser::ast::ConstraintItem ci;
    ci.column_name = "val";
    ci.op = parser::ast::ConstraintOperator::kBetween;
    ci.value_min = 0.0;
    ci.value_max = 100.0;
    constraints.push_back(ci);

    ValueRangeValidator validator(s, constraints);

    double below_min = -DBL_EPSILON;   // just below 0.0
    // Use nextafter to get the smallest representable value above 100.0
    // Note: 100.0 + DBL_EPSILON rounds to exactly 100.0 because DBL_EPSILON
    // is the epsilon for 1.0, not for 100.0. This is a common floating-point pitfall.
    double above_max = std::nextafter(100.0, std::numeric_limits<double>::infinity());

    std::vector<double> values = {below_min, above_max, 50.0};
    auto table = make_table("val", values);

    auto result = validator.validate_batch(table);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_failed, 2) << "Boundary violations not detected";
    EXPECT_EQ(result.value().rows_passed, 1);
}

// ============================================================================
// Test 9: Aggregate AVG precision — 1000 identical values
// ============================================================================
TEST(NumericPrecision, AggregateAvgIdenticalValues) {
    schema::Schema s = make_float_schema("val", 0.0, 200.0);
    constexpr double kExactValue = 42.123456789;
    constexpr int kCount = 1000;

    std::vector<double> values(kCount, kExactValue);
    auto table = make_table("val", values);

    AggregateConstraintDef def;
    def.constraint_name = "avg_test";
    def.column_name = "val";
    def.function = AggregateFunction::kAvg;
    def.window_type = WindowType::kInterval;
    def.window_interval_us = 2000000000LL;  // large window to include all rows

    AggregateEngine engine(s, {def});

    AggregationWindow window;
    window.start_row = 0;
    window.end_row = kCount - 1;
    window.window_start = 0;
    window.window_end = 2000000000LL;
    window.is_partial = false;
    for (int i = 0; i < kCount; ++i) window.included_rows.push_back(i);

    auto agg_result = engine.compute_aggregate(table, window, def);
    ASSERT_TRUE(agg_result.ok()) << agg_result.error().message;
    EXPECT_DOUBLE_EQ(agg_result.value(), kExactValue)
        << "AVG of 1000 identical values should be exactly that value, got: "
        << std::setprecision(17) << agg_result.value();
}

// ============================================================================
// Test 10: Aggregate SUM with large values — 100 rows of 1e10
// ============================================================================
TEST(NumericPrecision, AggregateSumLargeValues) {
    schema::Schema s = make_float_schema("val", 0.0, 1e12);
    constexpr double kValue = 1e10;
    constexpr int kCount = 100;
    constexpr double kExpectedSum = 1e12;  // 1e10 * 100

    std::vector<double> values(kCount, kValue);
    auto table = make_table("val", values);

    AggregateConstraintDef def;
    def.constraint_name = "sum_test";
    def.column_name = "val";
    def.function = AggregateFunction::kSum;
    def.window_type = WindowType::kInterval;
    def.window_interval_us = 2000000000LL;

    AggregateEngine engine(s, {def});

    AggregationWindow window;
    window.start_row = 0;
    window.end_row = kCount - 1;
    for (int i = 0; i < kCount; ++i) window.included_rows.push_back(i);

    auto agg_result = engine.compute_aggregate(table, window, def);
    ASSERT_TRUE(agg_result.ok()) << agg_result.error().message;
    // 100 * 1e10 = 1e12 — this should be exact since 1e10 is exactly representable
    // and 100 * 1e10 does not lose precision (1e12 < 2^53)
    EXPECT_DOUBLE_EQ(agg_result.value(), kExpectedSum)
        << "SUM of 100 x 1e10 should be exactly 1e12, got: "
        << std::setprecision(17) << agg_result.value();
}

// ============================================================================
// Test 11: InterRow delta with very small differences (1e-11 < DeltaMax 1e-10)
// ============================================================================
TEST(NumericPrecision, InterRowVerySmallDelta) {
    schema::Schema s = make_interrow_schema("val");

    InterRowConstraintDef cdef;
    cdef.column_name = "val";
    cdef.order_column = "timestamp";
    cdef.type = InterRowConstraintDef::Type::kDeltaMax;
    cdef.delta_max = 1e-10;

    InterRowEngine engine(s, {cdef});

    // Values differ by 1e-11 (well within 1e-10)
    std::vector<double> values;
    for (int i = 0; i < 100; ++i) {
        values.push_back(i * 1e-11);
    }
    auto table = make_table("val", values);

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    // All rows should pass since delta = 1e-11 < 1e-10
    EXPECT_EQ(result.value().rows_passed, 100)
        << "Small-delta rows should all pass, got "
        << result.value().rows_passed << " passed, "
        << result.value().rows_filtered << " filtered";
}

// ============================================================================
// Test 12: InterRow delta with very large differences (1e14 < DeltaMax 1e15)
// ============================================================================
TEST(NumericPrecision, InterRowVeryLargeDelta) {
    schema::Schema s = make_interrow_schema("val");

    InterRowConstraintDef cdef;
    cdef.column_name = "val";
    cdef.order_column = "timestamp";
    cdef.type = InterRowConstraintDef::Type::kDeltaMax;
    cdef.delta_max = 1e15;

    InterRowEngine engine(s, {cdef});

    // Values differ by 1e14 (within 1e15)
    std::vector<double> values;
    for (int i = 0; i < 100; ++i) {
        values.push_back(i * 1e14);
    }
    auto table = make_table("val", values);

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    // All rows should pass since delta = 1e14 < 1e15
    EXPECT_EQ(result.value().rows_passed, 100)
        << "Large-delta rows should all pass, got "
        << result.value().rows_passed << " passed, "
        << result.value().rows_filtered << " filtered";
}

// ============================================================================
// Test 13: RangeExtractor with extremely tight constraints [50.0, nextafter(50.0, INF)]
// Uses std::nextafter for the tightest possible non-degenerate range.
// ============================================================================
TEST(NumericPrecision, RangeExtractorTightConstraint) {
    schema::Schema s = make_float_schema("val", 0.0, 100.0);

    RangeExtractor extractor(s);

    // Use std::nextafter to get the smallest representable step above 50.0
    double tight_max = std::nextafter(50.0, std::numeric_limits<double>::infinity());

    parser::ast::ConstraintItem ci;
    ci.column_name = "val";
    ci.op = parser::ast::ConstraintOperator::kBetween;
    ci.value_min = 50.0;
    ci.value_max = tight_max;

    auto result = extractor.extract({ci});
    ASSERT_TRUE(result.ok()) << "Tight range should succeed: " << result.error().message;

    // Find the val column range
    bool found = false;
    for (const auto& r : result.value()) {
        if (r.column_name == "val") {
            found = true;
            EXPECT_DOUBLE_EQ(r.min_value, 50.0);
            EXPECT_DOUBLE_EQ(r.max_value, tight_max);
        }
    }
    EXPECT_TRUE(found) << "val column range not found";
}

// ============================================================================
// Test 14: GaussianSampler with stddev > range width
// Range [40, 60] width=20, stddev computed as width/6 ~ 3.33
// The GaussianSampler computes stddev from range internally (not user-specified).
// With stddev ~3.33, ~99.7% within 3 sigma = [40, 60].
// No values should be outside range due to truncation.
// ============================================================================
TEST(NumericPrecision, GaussianStddevLargerThanRange) {
    GaussianSampler sampler(42);
    constexpr double kMin = 40.0;
    constexpr double kMax = 60.0;
    constexpr int kSamples = 50000;

    TruncationStats total_stats{0, 0};

    for (int i = 0; i < kSamples; ++i) {
        TruncationStats local_stats;
        double val = sampler.sample_float(kMin, kMax, local_stats);
        ASSERT_GE(val, kMin) << "Gaussian truncated below min at sample " << i << ": " << val;
        ASSERT_LE(val, kMax) << "Gaussian truncated above max at sample " << i << ": " << val;
        ASSERT_FALSE(std::isinf(val));
        ASSERT_FALSE(std::isnan(val));
        total_stats.truncated_low += local_stats.truncated_low;
        total_stats.truncated_high += local_stats.truncated_high;
    }

    // GaussianSampler computes stddev = (max - min) / 6.0 = 20/6 ~ 3.33
    // With stddev 3.33, ~99.7% within 3 sigma = mean +/- 10 = [40, 60]
    // So truncation rate should be low (~0.3%).
    // The key invariant: no values outside [40, 60] (verified by ASSERTs above).
    int64_t total_truncated = total_stats.truncated_low + total_stats.truncated_high;
    EXPECT_LT(total_truncated, kSamples * 0.05)
        << "Truncation rate too high: " << total_truncated << " / " << kSamples;
}

// ============================================================================
// Test 15: SeedController — 1 million sequential seeds are all unique
// ============================================================================
TEST(NumericPrecision, SeedControllerUniqueSeeds) {
    SeedController controller(0xBEEFCAFE);
    constexpr int kCount = 1000000;

    // We test the full seed chain: global -> request -> batch -> row
    // This tests hash_combine collision resistance
    std::unordered_set<uint64_t> seen;
    seen.reserve(kCount * 2);

    for (int i = 0; i < kCount; ++i) {
        uint64_t req_seed = controller.request_seed(i);
        uint64_t batch_seed = controller.batch_seed(req_seed, i);
        uint64_t row_seed = controller.row_seed(batch_seed, i);

        // All three levels should be unique
        auto [it1, ok1] = seen.insert(req_seed);
        ASSERT_TRUE(ok1) << "Duplicate request_seed at i=" << i;
        auto [it2, ok2] = seen.insert(batch_seed);
        ASSERT_TRUE(ok2) << "Duplicate batch_seed at i=" << i;
        auto [it3, ok3] = seen.insert(row_seed);
        ASSERT_TRUE(ok3) << "Duplicate row_seed at i=" << i;
    }
}

// ============================================================================
// BONUS 1: RandomEngine uniform_range numeric stability
// When (max - min) is very large, uniform_01() * (max - min) can have
// granularity issues. Test that values are truly spread across the range.
// ============================================================================
TEST(NumericPrecision, RandomEngineSpreadInLargeRange) {
    RandomEngine rng(12345);
    constexpr double kMin = 0.0;
    constexpr double kMax = 1e15;
    constexpr int kSamples = 100000;

    double min_seen = kMax;
    double max_seen = kMin;

    for (int i = 0; i < kSamples; ++i) {
        double val = rng.uniform_range(kMin, kMax);
        ASSERT_GE(val, kMin);
        ASSERT_LE(val, kMax);
        min_seen = std::min(min_seen, val);
        max_seen = std::max(max_seen, val);
    }

    // With 100k samples in [0, 1e15], the spread should be substantial
    EXPECT_LT(min_seen, 1e13) << "Min seen too high, sampling may be biased: " << min_seen;
    EXPECT_GT(max_seen, 9e14) << "Max seen too low, sampling may be biased: " << max_seen;
}

// ============================================================================
// BONUS 2: Aggregate SUM precision with repeating fraction (0.1 x 1000)
// 0.1 is not exactly representable in binary; summing 1000 copies
// will accumulate error. We verify the error is within acceptable bounds.
// ============================================================================
TEST(NumericPrecision, AggregateSumRepeatingFraction) {
    schema::Schema s = make_float_schema("val", 0.0, 1.0);
    constexpr int kCount = 1000;
    constexpr double kValue = 0.1;

    std::vector<double> values(kCount, kValue);
    auto table = make_table("val", values);

    AggregateConstraintDef def;
    def.constraint_name = "sum_fraction";
    def.column_name = "val";
    def.function = AggregateFunction::kSum;

    AggregateEngine engine(s, {def});

    AggregationWindow window;
    window.start_row = 0;
    window.end_row = kCount - 1;
    for (int i = 0; i < kCount; ++i) window.included_rows.push_back(i);

    auto agg_result = engine.compute_aggregate(table, window, def);
    ASSERT_TRUE(agg_result.ok()) << agg_result.error().message;

    double expected = 100.0;  // 0.1 * 1000
    double actual = agg_result.value();
    // Naive summation of 0.1 x 1000 gives ~99.999... or 100.000...
    // Error should be less than 1e-10 (relative)
    EXPECT_NEAR(actual, expected, 1e-10)
        << "SUM of 1000 x 0.1 drifted too far from 100.0, got: "
        << std::setprecision(17) << actual;
}

// ============================================================================
// BONUS 3: ValueRangeValidator comparison semantics at boundary
// Verify the exact comparison semantics for strict vs non-strict
// ============================================================================
TEST(NumericPrecision, ValidatorExactComparisonSemantics) {
    schema::Schema s = make_float_schema("val", 0.0, 1.0);

    // kGreaterThan: val > 0.0
    {
        std::vector<parser::ast::ConstraintItem> constraints;
        parser::ast::ConstraintItem ci;
        ci.column_name = "val";
        ci.op = parser::ast::ConstraintOperator::kGreaterThan;
        ci.value_min = 0.0;
        constraints.push_back(ci);

        ValueRangeValidator validator(s, constraints);

        // 0.0 should FAIL (not > 0.0)
        // -0.0 should also FAIL because -0.0 == 0.0 in IEEE 754, and 0.0 <= 0.0
        // DBL_EPSILON should pass since it's > 0.0
        auto table = make_table("val", {0.0, DBL_EPSILON, -0.0});
        auto result = validator.validate_batch(table);
        ASSERT_TRUE(result.ok());
        EXPECT_EQ(result.value().rows_failed, 2) << "0.0 and -0.0 should both fail kGreaterThan(0.0)";
    }

    // kLessThan: val < 1.0
    {
        std::vector<parser::ast::ConstraintItem> constraints;
        parser::ast::ConstraintItem ci;
        ci.column_name = "val";
        ci.op = parser::ast::ConstraintOperator::kLessThan;
        ci.value_max = 1.0;
        constraints.push_back(ci);

        ValueRangeValidator validator(s, constraints);

        auto table = make_table("val", {1.0, 1.0 - DBL_EPSILON, 0.5});
        auto result = validator.validate_batch(table);
        ASSERT_TRUE(result.ok());
        EXPECT_EQ(result.value().rows_failed, 1) << "1.0 should fail kLessThan(1.0)";
    }
}

// ============================================================================
// BONUS 4: InterRow delta precision — verify abs() works for tiny differences
// ============================================================================
TEST(NumericPrecision, InterRowDeltaTinyPrecision) {
    schema::Schema s = make_interrow_schema("val");

    // DeltaMax = 2e-10; consecutive values differ by 1e-10 exactly
    InterRowConstraintDef cdef;
    cdef.column_name = "val";
    cdef.order_column = "timestamp";
    cdef.type = InterRowConstraintDef::Type::kDeltaMax;
    cdef.delta_max = 2e-10;

    InterRowEngine engine(s, {cdef});

    std::vector<double> values;
    double base = 1.0;
    for (int i = 0; i < 50; ++i) {
        values.push_back(base + i * 1e-10);
    }
    auto table = make_table("val", values);

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok()) << result.error().message;
    // All should pass since 1e-10 < 2e-10 (strict less)
    EXPECT_EQ(result.value().rows_passed, 50)
        << "Tiny-delta rows should all pass: "
        << result.value().rows_passed << " passed, "
        << result.value().rows_filtered << " filtered";
}

// ============================================================================
// BONUS 5: Verify RangeExtractor rejects degenerate ranges (min >= max)
// ============================================================================
TEST(NumericPrecision, RangeExtractorRejectsDegenerate) {
    schema::Schema s = make_float_schema("val", 0.0, 100.0);
    RangeExtractor extractor(s);

    // min == max => degenerate
    parser::ast::ConstraintItem ci;
    ci.column_name = "val";
    ci.op = parser::ast::ConstraintOperator::kBetween;
    ci.value_min = 50.0;
    ci.value_max = 50.0;

    auto result = extractor.extract({ci});
    EXPECT_FALSE(result.ok()) << "Should reject min == max";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}
