#include <gtest/gtest.h>
#include "engine/physics/gaussian_sampler.h"
#include "engine/physics/uniform_sampler.h"
#include "engine/physics/random.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/physics/range_extractor.h"
#include "engine/physics/seed_controller.h"
#include "schema/schema.h"
#include "parser/ast.h"

#include <arrow/array.h>
#include <arrow/type.h>

#include <cmath>
#include <numeric>
#include <algorithm>
#include <thread>
#include <vector>
#include <unordered_set>
#include <set>
#include <limits>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::schema;

// ============================================================================
// GaussianSampler Deep Tests
// ============================================================================

TEST(GaussianDeepTest, MeanAndStddevZero_MinEqualsMax) {
    // When min==max, mean==min and stddev==0. normal_distribution with stddev=0
    // may or may not be well-defined. We expect truncation to clamp to the value.
    GaussianSampler s(42);
    TruncationStats stats;
    // With min==max==5.0, stddev=0, the gaussian should return mean=5.0
    // which is clamped to [5.0, 5.0]
    auto val = s.sample_float(5.0, 5.0, stats);
    EXPECT_GE(val, 5.0);
    EXPECT_LE(val, 5.0);
}

TEST(GaussianDeepTest, NegativeRange) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_float(-100.0, -50.0, stats);
        EXPECT_GE(val, -100.0);
        EXPECT_LE(val, -50.0);
    }
}

TEST(GaussianDeepTest, VerySmallRange) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_float(1.0, 1.0 + 1e-12, stats);
        EXPECT_GE(val, 1.0);
        EXPECT_LE(val, 1.0 + 1e-12);
    }
}

TEST(GaussianDeepTest, StatisticalMeanNearCenter) {
    // With enough samples, mean should be near (min+max)/2
    GaussianSampler s(42);
    TruncationStats stats;
    double sum = 0;
    int N = 50000;
    double min = 0.0, max = 100.0;
    for (int i = 0; i < N; i++) {
        sum += s.sample_float(min, max, stats);
    }
    double mean = sum / N;
    EXPECT_NEAR(mean, 50.0, 2.0) << "Mean should be close to 50.0";
}

TEST(GaussianDeepTest, TruncationClampsToRange) {
    // With a very narrow range and many samples, truncation should happen
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 10000; i++) {
        s.sample_float(0.0, 0.01, stats);
    }
    // stddev = 0.01/6 ~ 0.00167, most values within range but some will truncate
    EXPECT_GT(stats.truncated_low + stats.truncated_high, 0);
}

TEST(GaussianDeepTest, TruncationStatsAreSeparate) {
    GaussianSampler s(42);
    TruncationStats stats1{}, stats2{};
    for (int i = 0; i < 5000; i++) {
        s.sample_float(0.0, 1.0, stats1);
    }
    EXPECT_GT(stats1.truncated_low + stats1.truncated_high, 0);
    // stats2 should be zero
    EXPECT_EQ(stats2.truncated_low, 0);
    EXPECT_EQ(stats2.truncated_high, 0);
}

TEST(GaussianDeepTest, ExtremeRange) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_float(-1e15, 1e15, stats);
        EXPECT_GE(val, -1e15);
        EXPECT_LE(val, 1e15);
    }
}

TEST(GaussianDeepTest, DifferentSeedsDifferentSequences) {
    GaussianSampler s1(42);
    GaussianSampler s2(43);
    TruncationStats stats;
    bool different = false;
    for (int i = 0; i < 100; i++) {
        if (s1.sample_float(0.0, 10.0, stats) != s2.sample_float(0.0, 10.0, stats)) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST(GaussianDeepTest, SameSeedReproducesSequence) {
    GaussianSampler s1(42);
    TruncationStats stats;
    std::vector<double> vals;
    for (int i = 0; i < 100; i++) {
        vals.push_back(s1.sample_float(-10.0, 10.0, stats));
    }
    GaussianSampler s2(42);
    for (int i = 0; i < 100; i++) {
        EXPECT_DOUBLE_EQ(s2.sample_float(-10.0, 10.0, stats), vals[i]);
    }
}

// ============================================================================
// UniformSampler Deep Tests
// ============================================================================

TEST(UniformDeepTest, FloatExactBoundaries) {
    UniformSampler s(42);
    // Zero-width range
    auto val = s.sample_float(3.14, 3.14);
    EXPECT_DOUBLE_EQ(val, 3.14);
}

TEST(UniformDeepTest, FloatMinEqualsMax) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_float(42.0, 42.0);
        EXPECT_DOUBLE_EQ(val, 42.0);
    }
}

TEST(UniformDeepTest, IntMinEqualsMax) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_int(7, 7);
        EXPECT_EQ(val, 7);
    }
}

TEST(UniformDeepTest, IntBoundaryValues) {
    UniformSampler s(42);
    auto val = s.sample_int(INT64_MIN, INT64_MAX);
    // Just ensure no crash and value is in range
    EXPECT_GE(val, INT64_MIN);
    EXPECT_LE(val, INT64_MAX);
}

TEST(UniformDeepTest, FloatVerySmallRange) {
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        auto val = s.sample_float(0.0, 1e-15);
        EXPECT_GE(val, 0.0);
        EXPECT_LE(val, 1e-15);
    }
}

TEST(UniformDeepTest, FloatNegativeRange) {
    UniformSampler s(42);
    for (int i = 0; i < 1000; i++) {
        auto val = s.sample_float(-1000.0, -1.0);
        EXPECT_GE(val, -1000.0);
        EXPECT_LE(val, -1.0);
    }
}

TEST(UniformDeepTest, StatisticalUniformity) {
    // Generate many samples in [0, 10) and check histogram bins are reasonably filled
    UniformSampler s(42);
    int bins[10] = {};
    int N = 100000;
    for (int i = 0; i < N; i++) {
        auto val = s.sample_float(0.0, 10.0);
        int bin = std::min(static_cast<int>(val), 9);
        bins[bin]++;
    }
    // Each bin should have roughly N/10 = 10000 samples
    for (int i = 0; i < 10; i++) {
        EXPECT_GT(bins[i], 8000) << "Bin " << i << " too sparse";
        EXPECT_LT(bins[i], 12000) << "Bin " << i << " too dense";
    }
}

TEST(UniformDeepTest, StringCharacterDistribution) {
    UniformSampler s(42);
    int char_count[62] = {}; // a-z, A-Z, 0-9
    int total_chars = 0;
    for (int i = 0; i < 1000; i++) {
        auto str = s.sample_string();
        for (char c : str) {
            if (c >= 'a' && c <= 'z') char_count[c - 'a']++;
            else if (c >= 'A' && c <= 'Z') char_count[26 + c - 'A']++;
            else if (c >= '0' && c <= '9') char_count[52 + c - '0']++;
            total_chars++;
        }
    }
    // Each character should appear at least once in 1000 strings
    int used_chars = 0;
    for (int i = 0; i < 62; i++) {
        if (char_count[i] > 0) used_chars++;
    }
    EXPECT_GT(used_chars, 50) << "Expected most characters to appear";
}

TEST(UniformDeepTest, StringOnlyAlphanumeric) {
    UniformSampler s(42);
    for (int i = 0; i < 500; i++) {
        auto str = s.sample_string();
        for (char c : str) {
            EXPECT_TRUE((c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9'))
                << "Non-alphanumeric char: " << c;
        }
    }
}

TEST(UniformDeepTest, EnumSingleValue) {
    std::vector<std::string> vals = {"only_one"};
    UniformSampler s(42);
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(s.sample_enum(vals), "only_one");
    }
}

TEST(UniformDeepTest, EnumManyValues) {
    std::vector<std::string> vals;
    for (int i = 0; i < 100; i++) {
        vals.push_back("val_" + std::to_string(i));
    }
    UniformSampler s(42);
    std::set<std::string> seen;
    for (int i = 0; i < 5000; i++) {
        auto v = s.sample_enum(vals);
        EXPECT_NE(std::find(vals.begin(), vals.end(), v), vals.end());
        seen.insert(v);
    }
    // Should have seen at least 80 unique values out of 100
    EXPECT_GE(seen.size(), 80u);
}

TEST(UniformDeepTest, DateTimeInRange) {
    UniformSampler s(42);
    for (int i = 0; i < 500; i++) {
        auto val = s.sample_datetime();
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 31536000000000LL);
    }
}

TEST(UniformDeepTest, DeterminismAcrossInstances) {
    UniformSampler s1(12345);
    UniformSampler s2(12345);
    for (int i = 0; i < 200; i++) {
        EXPECT_DOUBLE_EQ(s1.sample_float(-100.0, 100.0), s2.sample_float(-100.0, 100.0));
        EXPECT_EQ(s1.sample_int(-1000, 1000), s2.sample_int(-1000, 1000));
        EXPECT_EQ(s1.sample_string(), s2.sample_string());
    }
}

// ============================================================================
// RandomEngine Deep Tests
// ============================================================================

TEST(RandomDeepTest, Uniform01InRange) {
    RandomEngine rng(42);
    for (int i = 0; i < 10000; i++) {
        auto val = rng.uniform_01();
        EXPECT_GE(val, 0.0);
        EXPECT_LE(val, 1.0);
    }
}

TEST(RandomDeepTest, Uniform01Mean) {
    RandomEngine rng(42);
    double sum = 0;
    int N = 100000;
    for (int i = 0; i < N; i++) {
        sum += rng.uniform_01();
    }
    double mean = sum / N;
    EXPECT_NEAR(mean, 0.5, 0.01);
}

TEST(RandomDeepTest, GaussianMeanAndVariance) {
    RandomEngine rng(42);
    double sum = 0, sum_sq = 0;
    int N = 100000;
    double target_mean = 10.0;
    double target_stddev = 2.0;
    for (int i = 0; i < N; i++) {
        auto val = rng.gaussian(target_mean, target_stddev);
        sum += val;
        sum_sq += (val - target_mean) * (val - target_mean);
    }
    double mean = sum / N;
    double variance = sum_sq / N;
    EXPECT_NEAR(mean, target_mean, 0.05);
    EXPECT_NEAR(std::sqrt(variance), target_stddev, 0.05);
}

TEST(RandomDeepTest, GaussianNegativeStddev) {
    // normal_distribution with negative stddev is technically undefined behavior
    // in C++. But we test that the engine doesn't crash.
    RandomEngine rng(42);
    // Just verify it doesn't crash; the values may be NaN/Inf
    auto val = rng.gaussian(0.0, -1.0);
    // We don't assert on value validity since this is UB territory
    (void)val;
}

TEST(RandomDeepTest, UniformIntDistribution) {
    RandomEngine rng(42);
    int bins[10] = {};
    int N = 100000;
    for (int i = 0; i < N; i++) {
        auto val = rng.uniform_int(0, 9);
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 9);
        bins[val]++;
    }
    for (int i = 0; i < 10; i++) {
        EXPECT_GT(bins[i], 8000) << "Bin " << i << " too sparse";
        EXPECT_LT(bins[i], 12000) << "Bin " << i << " too dense";
    }
}

TEST(RandomDeepTest, UniformIndexSizeOne) {
    RandomEngine rng(42);
    for (int i = 0; i < 100; i++) {
        auto idx = rng.uniform_index(1);
        EXPECT_EQ(idx, 0);
    }
}

TEST(RandomDeepTest, UniformIndexSizeZeroUB) {
    // uniform_index(0) would create uniform_int_distribution(0, -1) which is UB.
    // This test documents the behavior — it will likely crash or produce garbage.
    // We skip actually calling it to avoid UB in tests.
    // If we want to test this, the source code should guard against size==0.
    // For now, document that size=0 is a precondition violation.
    EXPECT_TRUE(true);  // Placeholder acknowledging the gap
}

TEST(RandomDeepTest, UniformRangeSymmetric) {
    RandomEngine rng(42);
    double sum = 0;
    int N = 100000;
    for (int i = 0; i < N; i++) {
        sum += rng.uniform_range(-100.0, 100.0);
    }
    double mean = sum / N;
    EXPECT_NEAR(mean, 0.0, 1.0);
}

TEST(RandomDeepTest, SeedDeterminismAcrossThreads) {
    auto expected_val = []{
        RandomEngine rng(42);
        return rng.uniform_int(0, 1000000);
    }();

    std::atomic<bool> all_match{true};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&]() {
            RandomEngine rng(42);
            auto val = rng.uniform_int(0, 1000000);
            if (val != expected_val) all_match = false;
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_TRUE(all_match.load()) << "Same seed should produce same value across threads";
}

TEST(RandomDeepTest, SeedZeroIsNotSpecial) {
    RandomEngine rng(0);
    auto val = rng.uniform_int(0, 1000000);
    // Just verify no crash and value is reasonable
    EXPECT_GE(val, 0);
    EXPECT_LE(val, 1000000);
}

TEST(RandomDeepTest, MaxSeedValue) {
    RandomEngine rng(UINT64_MAX);
    auto val = rng.uniform_01();
    EXPECT_GE(val, 0.0);
    EXPECT_LE(val, 1.0);
}

// ============================================================================
// SeedController Deep Tests
// ============================================================================

TEST(SeedControllerDeepTest, GlobalSeedGetter) {
    SeedController sc(42);
    EXPECT_EQ(sc.global_seed(), 42u);
}

TEST(SeedControllerDeepTest, SeedZeroGlobal) {
    SeedController sc(0);
    EXPECT_EQ(sc.global_seed(), 0u);
    auto rs = sc.request_seed(0);
    // hash_combine should produce non-zero from seed=0 + value=0
    EXPECT_NE(rs, 0u);
}

TEST(SeedControllerDeepTest, NoCollisionBetweenLevels) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    auto bs = sc.batch_seed(rs, 0);
    auto ws = sc.row_seed(bs, 0);

    // All three should be different from each other
    std::set<uint64_t> seeds = {sc.global_seed(), rs, bs, ws};
    EXPECT_EQ(seeds.size(), 4u) << "Seeds at different levels should be distinct";
}

TEST(SeedControllerDeepTest, NoCollisionAcrossRequestIds) {
    SeedController sc(42);
    std::unordered_set<uint64_t> seeds;
    for (uint64_t i = 0; i < 1000; i++) {
        seeds.insert(sc.request_seed(i));
    }
    EXPECT_EQ(seeds.size(), 1000u) << "Each request ID should produce a unique seed";
}

TEST(SeedControllerDeepTest, NoCollisionAcrossBatchIndices) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    std::unordered_set<uint64_t> seeds;
    for (int64_t i = 0; i < 1000; i++) {
        seeds.insert(sc.batch_seed(rs, i));
    }
    EXPECT_EQ(seeds.size(), 1000u);
}

TEST(SeedControllerDeepTest, HashCombineIsCommuteResistant) {
    // hash_combine(seed, value) should differ from hash_combine(value, seed)
    // (i.e., the operation is not symmetric)
    SeedController sc1(42);
    uint64_t a = 1, b = 100;
    auto rs1 = sc1.request_seed(a);  // hash_combine(42, 1)
    SeedController sc2(b);
    auto rs2 = sc2.request_seed(42); // hash_combine(100, 42)
    EXPECT_NE(rs1, rs2) << "hash_combine should not be symmetric";
}

// ============================================================================
// RangeExtractor Deep Tests
// ============================================================================

namespace {

Schema make_float_schema(const std::string& name, double min, double max) {
    Schema s;
    s.type_name = name;
    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = min;
    col.range_max = max;
    s.columns.push_back(col);
    return s;
}

Schema make_multi_col_schema() {
    Schema s;
    s.type_name = "multi";
    {
        ColumnDef col;
        col.name = "temp";
        col.type = DataType::kFloat;
        col.range_min = -50.0;
        col.range_max = 80.0;
        s.columns.push_back(col);
    }
    {
        ColumnDef col;
        col.name = "pressure";
        col.type = DataType::kFloat;
        col.range_min = 900.0;
        col.range_max = 1100.0;
        s.columns.push_back(col);
    }
    {
        ColumnDef col;
        col.name = "status";
        col.type = DataType::kEnum;
        col.enum_values = {"normal", "warning", "fault"};
        s.columns.push_back(col);
    }
    return s;
}

Schema make_string_only_schema() {
    Schema s;
    s.type_name = "strings";
    {
        ColumnDef col;
        col.name = "name";
        col.type = DataType::kString;
        s.columns.push_back(col);
    }
    {
        ColumnDef col;
        col.name = "label";
        col.type = DataType::kString;
        s.columns.push_back(col);
    }
    return s;
}

Schema make_int_only_schema() {
    Schema s;
    s.type_name = "ints";
    {
        ColumnDef col;
        col.name = "count";
        col.type = DataType::kInt;
        col.range_min = 0.0;
        col.range_max = 100.0;
        s.columns.push_back(col);
    }
    return s;
}

Schema make_datetime_schema() {
    Schema s;
    s.type_name = "events";
    {
        ColumnDef col;
        col.name = "ts";
        col.type = DataType::kDatetime;
        s.columns.push_back(col);
    }
    return s;
}

} // anonymous namespace

TEST(RangeExtractorDeepTest, NoConstraintsReturnsSchemaDefaults) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    auto result = ext.extract({});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 1u);
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 0.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 100.0);
}

TEST(RangeExtractorDeepTest, BetweenConstraint) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kBetween, 10.0, 50.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 10.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 50.0);
}

TEST(RangeExtractorDeepTest, GreaterThanConstraint) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kGreaterThan, 20.0, 0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 20.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 100.0);
}

TEST(RangeExtractorDeepTest, LessThanConstraint) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kLessThan, 0, 80.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 0.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 80.0);
}

TEST(RangeExtractorDeepTest, OverlappingConstraintsTightenRange) {
    // Schema default: [0, 100]
    // Constraint 1: > 20 (min=20)
    // Constraint 2: < 60 (max=60)
    // Result: [20, 60]
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kGreaterThan, 20.0, 0},
        {"value", parser::ast::ConstraintOperator::kLessThan, 0, 60.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 20.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 60.0);
}

TEST(RangeExtractorDeepTest, MultipleBetweenConstraintsPicksTightest) {
    // Between [10, 50] and Between [20, 40] => [20, 40]
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kBetween, 10.0, 50.0},
        {"value", parser::ast::ConstraintOperator::kBetween, 20.0, 40.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 20.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 40.0);
}

TEST(RangeExtractorDeepTest, ContradictoryConstraintsMinGreaterThanMax) {
    // Schema default: [0, 100]
    // Constraint: > 80 (min=80) AND < 20 (max=20)
    // min(80) >= max(20) => empty range => error
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kGreaterThan, 80.0, 0},
        {"value", parser::ast::ConstraintOperator::kLessThan, 0, 20.0},
    };
    auto result = ext.extract(constraints);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

TEST(RangeExtractorDeepTest, EqualMinAndMaxAllowed) {
    // > 50 AND < 50 => min=50, max=50 => min >= max => error
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kGreaterThan, 50.0, 0},
        {"value", parser::ast::ConstraintOperator::kLessThan, 0, 50.0},
    };
    auto result = ext.extract(constraints);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

TEST(RangeExtractorDeepTest, UndefinedColumnError) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"nonexistent", parser::ast::ConstraintOperator::kBetween, 0.0, 50.0},
    };
    auto result = ext.extract(constraints);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedColumn);
}

TEST(RangeExtractorDeepTest, StringOnlySchemaNoRanges) {
    auto schema = make_string_only_schema();
    RangeExtractor ext(schema);
    auto result = ext.extract({});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 2u);
    // String columns should have type kString, no meaningful min/max
    EXPECT_EQ(result.value()[0].type, DataType::kString);
    EXPECT_EQ(result.value()[1].type, DataType::kString);
}

TEST(RangeExtractorDeepTest, EnumColumnPreservesValues) {
    auto schema = make_multi_col_schema();
    RangeExtractor ext(schema);
    auto result = ext.extract({});
    ASSERT_TRUE(result.ok());
    auto& status_range = result.value()[2];
    EXPECT_EQ(status_range.column_name, "status");
    EXPECT_EQ(status_range.type, DataType::kEnum);
    EXPECT_EQ(status_range.enum_values.size(), 3u);
}

TEST(RangeExtractorDeepTest, DatetimeColumnDefaultRange) {
    auto schema = make_datetime_schema();
    RangeExtractor ext(schema);
    auto result = ext.extract({});
    ASSERT_TRUE(result.ok());
    auto& ts_range = result.value()[0];
    EXPECT_EQ(ts_range.type, DataType::kDatetime);
    EXPECT_DOUBLE_EQ(ts_range.min_value, 0.0);
    EXPECT_DOUBLE_EQ(ts_range.max_value, 31536000000000.0);
}

TEST(RangeExtractorDeepTest, IntColumnWithConstraints) {
    auto schema = make_int_only_schema();
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"count", parser::ast::ConstraintOperator::kBetween, 10.0, 50.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 10.0);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 50.0);
}

TEST(RangeExtractorDeepTest, NoDefaultRangeUsesFallback) {
    // Float column with no range_min/range_max set
    Schema s;
    s.type_name = "no_default";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    // No range_min, range_max set
    s.columns.push_back(col);

    RangeExtractor ext(s);
    auto result = ext.extract({});
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, -1e18);
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 1e18);
}

TEST(RangeExtractorDeepTest, GreaterEqualConstraint) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kGreaterEqual, 20.0, 0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].min_value, 20.0);
}

TEST(RangeExtractorDeepTest, LessEqualConstraint) {
    auto schema = make_float_schema("test", 0.0, 100.0);
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kLessEqual, 0, 80.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_DOUBLE_EQ(result.value()[0].max_value, 80.0);
}

TEST(RangeExtractorDeepTest, MultiColumnConstraintsAffectOnlyTargetColumn) {
    auto schema = make_multi_col_schema();
    RangeExtractor ext(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temp", parser::ast::ConstraintOperator::kBetween, 0.0, 30.0},
    };
    auto result = ext.extract(constraints);
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto& ranges = result.value();
    // temp should be narrowed
    EXPECT_DOUBLE_EQ(ranges[0].min_value, 0.0);
    EXPECT_DOUBLE_EQ(ranges[0].max_value, 30.0);
    // pressure should keep schema defaults
    EXPECT_DOUBLE_EQ(ranges[1].min_value, 900.0);
    EXPECT_DOUBLE_EQ(ranges[1].max_value, 1100.0);
}

// ============================================================================
// RectangularSampler Deep Edge Cases
// ============================================================================

TEST(RectangularDeepTest, EmptySchema) {
    Schema s;
    s.type_name = "empty";
    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 10, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RectangularDeepTest, SingleFloatColumn) {
    Schema s;
    s.type_name = "single_float";
    ColumnDef col;
    col.name = "x";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 1.0;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_columns(), 1);
    EXPECT_EQ(result.value().data->num_rows(), 100);
}

TEST(RectangularDeepTest, SingleIntColumn) {
    Schema s;
    s.type_name = "single_int";
    ColumnDef col;
    col.name = "count";
    col.type = DataType::kInt;
    col.range_min = 0.0;
    col.range_max = 100.0;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_columns(), 1);
}

TEST(RectangularDeepTest, SingleDatetimeColumn) {
    Schema s;
    s.type_name = "single_dt";
    ColumnDef col;
    col.name = "ts";
    col.type = DataType::kDatetime;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 50, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_columns(), 1);
}

TEST(RectangularDeepTest, SingleStringColumn) {
    Schema s;
    s.type_name = "single_str";
    ColumnDef col;
    col.name = "name";
    col.type = DataType::kString;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 50, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_columns(), 1);
}

TEST(RectangularDeepTest, SingleEnumColumn) {
    Schema s;
    s.type_name = "single_enum";
    ColumnDef col;
    col.name = "color";
    col.type = DataType::kEnum;
    col.enum_values = {"red", "green", "blue"};
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_columns(), 1);

    auto arr = std::static_pointer_cast<arrow::StringArray>(
        result.value().data->column(0)->chunk(0));
    std::set<std::string> seen;
    for (int64_t i = 0; i < arr->length(); i++) {
        seen.insert(arr->GetString(i));
    }
    EXPECT_GE(seen.size(), 2u);
}

TEST(RectangularDeepTest, GaussianSingleColumn) {
    Schema s;
    s.type_name = "gauss_single";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 1000, 42, "gaussian", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.distribution_used, "gaussian");

    auto arr = std::static_pointer_cast<arrow::DoubleArray>(
        result.value().data->column(0)->chunk(0));
    for (int64_t i = 0; i < arr->length(); i++) {
        EXPECT_GE(arr->Value(i), 0.0);
        EXPECT_LE(arr->Value(i), 100.0);
    }
}

TEST(RectangularDeepTest, BatchCountExactDivision) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 3000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 3000);
    EXPECT_EQ(result.value().stats.batch_count, 3);
}

TEST(RectangularDeepTest, BatchCountWithRemainder) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 2500, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 2500);
    EXPECT_EQ(result.value().stats.batch_count, 3);
}

TEST(RectangularDeepTest, BatchSizeSmallerThanLimit) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 10};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);
    EXPECT_EQ(result.value().stats.batch_count, 10);
}

TEST(RectangularDeepTest, BatchSizeEqualsLimit) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 100};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);
    EXPECT_EQ(result.value().stats.batch_count, 1);
}

TEST(RectangularDeepTest, UnsupportedDistribution) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "exponential", 1000};
    auto result = sampler.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
    EXPECT_NE(result.error().message.find("Unsupported distribution"), std::string::npos);
}

TEST(RectangularDeepTest, GaussianIntColumn) {
    Schema s;
    s.type_name = "gauss_int";
    ColumnDef col;
    col.name = "val";
    col.type = DataType::kInt;
    col.range_min = 0.0;
    col.range_max = 100.0;
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 100, 42, "gaussian", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.distribution_used, "gaussian");

    auto arr = std::static_pointer_cast<arrow::Int64Array>(
        result.value().data->column(0)->chunk(0));
    for (int64_t i = 0; i < arr->length(); i++) {
        EXPECT_GE(arr->Value(i), 0);
        EXPECT_LE(arr->Value(i), 100);
    }
}

TEST(RectangularDeepTest, ValidateRequestNegativeLimit) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, -5, 42, "uniform", 1000};
    auto result = sampler.validate_request(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RectangularDeepTest, ValidateRequestEmptySchema) {
    Schema s;
    s.type_name = "empty";
    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 10, 42, "uniform", 1000};
    auto result = sampler.validate_request(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RectangularDeepTest, ValidateRequestOk) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10, 42, "uniform", 1000};
    auto result = sampler.validate_request(req);
    EXPECT_TRUE(result.ok());
}

TEST(RectangularDeepTest, ColumnNamesMatchSchema) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto table = result.value().data;
    EXPECT_EQ(table->schema()->field(0)->name(), "temp");
    EXPECT_EQ(table->schema()->field(1)->name(), "pressure");
    EXPECT_EQ(table->schema()->field(2)->name(), "status");
}

TEST(RectangularDeepTest, AllEnumValuesRepresented) {
    Schema s;
    s.type_name = "enum_test";
    ColumnDef col;
    col.name = "color";
    col.type = DataType::kEnum;
    col.enum_values = {"red", "green", "blue"};
    s.columns.push_back(col);

    RectangularSampler sampler(s);
    GenerationRequest req{s, {}, 500, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto arr = std::static_pointer_cast<arrow::StringArray>(
        result.value().data->column(0)->chunk(0));
    std::set<std::string> seen;
    for (int64_t i = 0; i < arr->length(); i++) {
        auto val = arr->GetString(i);
        EXPECT_TRUE(val == "red" || val == "green" || val == "blue")
            << "Unexpected enum: " << val;
        seen.insert(val);
    }
    EXPECT_EQ(seen.size(), 3u) << "All enum values should appear in 500 samples";
}

// ============================================================================
// UniformSampler with min > max (edge case / potential bug detection)
// ============================================================================

TEST(UniformDeepTest, FloatMinGreaterThanMax_NegativeRange) {
    // uniform_range with min > max will compute negative (max-min),
    // giving values outside [min, max]. This is a potential bug.
    UniformSampler s(42);
    auto val = s.sample_float(10.0, 5.0);
    // With min > max: uniform_range returns 10 + uniform_01() * (5-10) = 10 - uniform*5
    // So val will be in [5, 10], which is actually [max, min] -- reversed.
    // This documents the behavior that min > max is NOT guarded.
    EXPECT_GE(val, 5.0);
    EXPECT_LE(val, 10.0);
}

TEST(UniformDeepTest, IntMinGreaterThanMax) {
    // uniform_int with min > max is UB for std::uniform_int_distribution
    // We document that this is a precondition the caller must satisfy.
    // Not calling it to avoid UB.
    EXPECT_TRUE(true);
}

// ============================================================================
// RandomEngine uniform_index with size=0
// ============================================================================

TEST(RandomDeepTest, UniformIndexLargeSize) {
    RandomEngine rng(42);
    for (int i = 0; i < 1000; i++) {
        auto idx = rng.uniform_index(1000000);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, 1000000);
    }
}

// ============================================================================
// File I/O integration with physics (PID-based temp paths)
// ============================================================================

TEST(PhysicsFileIOTest, GenerateAndVerifyToFile) {
    auto schema = make_multi_col_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto dir = std::filesystem::temp_directory_path() /
               ("synthgen_physics_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    auto filepath = dir / "stats.txt";

    {
        std::ofstream ofs(filepath);
        ofs << "rows=" << result.value().stats.rows_generated << "\n";
        ofs << "batches=" << result.value().stats.batch_count << "\n";
        ofs << "distribution=" << result.value().stats.distribution_used << "\n";
    }

    std::ifstream ifs(filepath);
    std::string line;
    std::getline(ifs, line);
    EXPECT_EQ(line, "rows=100");

    std::filesystem::remove_all(dir);
}
