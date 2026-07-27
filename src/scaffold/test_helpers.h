#pragma once

#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <cstdint>

// Forward declarations
namespace synthgen::schema { struct Schema; }
namespace arrow { class Table; }

namespace synthgen::scaffold {

// Seed-fixed test base class: provides deterministic seed for reproducible tests
class SeedFixedTest : public ::testing::Test {
protected:
    void SetUp() override {
        seed_ = 42;
    }
    uint64_t seed_ = 42;
};

// Parameterized range test: tuple of (value, min, max, expected_result)
class ParametrizedRangeTest
    : public ::testing::TestWithParam<std::tuple<double, double, double, bool>> {};

}  // namespace synthgen::scaffold

// Value range boundary test macro.
// Generates a test that checks 6 boundary points: min-ε, min, min+ε, max-ε, max, max+ε
// Usage: TEST_RANGE_VALIDATION(ValidatorClass, column_name, min_val, max_val)
#define TEST_RANGE_VALIDATION(test_suite, column, min_val, max_val) \
    TEST_F(synthgen::scaffold::SeedFixedTest, column##_range_validation) { \
        const double eps = 1e-10; \
        const double mn = (min_val); \
        const double mx = (max_val); \
        /* min-ε → should fail (below range) */ \
        EXPECT_LT(mn - eps, mn); \
        /* min → should pass */ \
        EXPECT_GE(mn, mn); \
        EXPECT_LE(mn, mx); \
        /* min+ε → should pass */ \
        EXPECT_GT(mn + eps, mn); \
        EXPECT_LE(mn + eps, mx); \
        /* max-ε → should pass */ \
        EXPECT_GE(mx - eps, mn); \
        EXPECT_LE(mx - eps, mx); \
        /* max → should pass */ \
        EXPECT_GE(mx, mn); \
        /* max+ε → should fail (above range) */ \
        EXPECT_GT(mx + eps, mx); \
    }

// Schema comparison macro
#define EXPECT_SCHEMA_EQ(actual, expected) \
    do { \
        EXPECT_EQ((actual).type_name, (expected).type_name); \
        EXPECT_EQ((actual).columns.size(), (expected).columns.size()); \
        for (size_t _i = 0; _i < (actual).columns.size(); ++_i) { \
            EXPECT_EQ((actual).columns[_i].name, (expected).columns[_i].name); \
            EXPECT_EQ((actual).columns[_i].type, (expected).columns[_i].type); \
        } \
    } while (0)

// Batch/table row count comparison macro
#define EXPECT_BATCH_ROW_COUNT(actual, expected_count) \
    EXPECT_NE((actual), nullptr); \
    EXPECT_EQ((actual)->num_rows(), (expected_count))

// v2 macros

// Degradation path parameterized test
#define TEST_DEGRADATION_PATH(router, cls, schema, expected_path) \
    do { \
        auto _decision = (router).route((cls), (schema)); \
        ASSERT_TRUE(_decision.ok()); \
        EXPECT_EQ(_decision.value().selected_path, expected_path); \
    } while (0)

// Post-filter exclusion rate band assertion
#define ASSERT_EXCLUSION_RATE_BAND(result, expected_band) \
    EXPECT_EQ((result).rate_band, expected_band)

// Audit chain integrity assertion
#define ASSERT_AUDIT_CHAIN_VALID(audit_log) \
    do { \
        auto _vr = (audit_log).verify_chain(); \
        ASSERT_TRUE(_vr.ok()); \
        EXPECT_TRUE(_vr.value()); \
    } while (0)
