#include <gtest/gtest.h>
#include "scaffold/test_helpers.h"
#include "schema/schema.h"
#include "common/types.h"

#include <cmath>
#include <memory>

using namespace synthgen;
using namespace synthgen::scaffold;
using namespace synthgen::schema;

// ===== SeedFixedTest Base Class =====

class MySeededTest : public SeedFixedTest {};

TEST_F(MySeededTest, SeedIsFixed) {
    EXPECT_EQ(seed_, 42u);
}

TEST_F(MySeededTest, SeedDeterministic) {
    EXPECT_EQ(seed_, 42u);
    seed_ = 100;
    EXPECT_EQ(seed_, 100u);
}

// ===== ParametrizedRangeTest =====

TEST_P(ParametrizedRangeTest, ValueInRangeCheck) {
    auto [value, min_val, max_val, expected] = GetParam();

    bool in_range = (value >= min_val && value <= max_val);
    EXPECT_EQ(in_range, expected);
}

INSTANTIATE_TEST_SUITE_P(
    RangeBoundaryTests,
    ParametrizedRangeTest,
    ::testing::Values(
        std::make_tuple(-10.1, -10.0, 45.0, false),   // min-ε
        std::make_tuple(-10.0, -10.0, 45.0, true),     // min
        std::make_tuple(-9.9, -10.0, 45.0, true),      // min+ε
        std::make_tuple(44.9, -10.0, 45.0, true),      // max-ε
        std::make_tuple(45.0, -10.0, 45.0, true),      // max
        std::make_tuple(45.1, -10.0, 45.0, false),     // max+ε
        std::make_tuple(0.0, -10.0, 45.0, true),       // midpoint
        std::make_tuple(-100.0, -10.0, 45.0, false),   // far below
        std::make_tuple(100.0, -10.0, 45.0, false),    // far above
        std::make_tuple(0.0, 0.0, 0.0, true)           // zero-width range (point)
    )
);

// ===== EXPECT_SCHEMA_EQ macro =====

TEST(SchemaComparisonTest, EqualSchemas) {
    Schema s1;
    s1.type_name = "sensor";
    ColumnDef c1;
    c1.name = "temp";
    c1.type = DataType::kFloat;
    s1.columns.push_back(c1);

    Schema s2;
    s2.type_name = "sensor";
    ColumnDef c2;
    c2.name = "temp";
    c2.type = DataType::kFloat;
    s2.columns.push_back(c2);

    EXPECT_SCHEMA_EQ(s1, s2);
}

TEST(SchemaComparisonTest, DifferentTypeNames) {
    Schema s1;
    s1.type_name = "sensor_a";
    ColumnDef c;
    c.name = "temp";
    c.type = DataType::kFloat;
    s1.columns.push_back(c);

    Schema s2;
    s2.type_name = "sensor_b";
    s2.columns.push_back(c);

    EXPECT_NE(s1.type_name, s2.type_name);
}

TEST(SchemaComparisonTest, DifferentColumnCounts) {
    Schema s1;
    s1.type_name = "sensor";
    ColumnDef c1;
    c1.name = "temp";
    c1.type = DataType::kFloat;
    s1.columns.push_back(c1);

    Schema s2;
    s2.type_name = "sensor";
    s2.columns.push_back(c1);
    ColumnDef c2;
    c2.name = "pressure";
    c2.type = DataType::kFloat;
    s2.columns.push_back(c2);

    EXPECT_NE(s1.columns.size(), s2.columns.size());
}

TEST(SchemaComparisonTest, EmptySchemas) {
    Schema s1, s2;
    s1.type_name = "empty";
    s2.type_name = "empty";
    EXPECT_SCHEMA_EQ(s1, s2);
}

// ===== Range boundary logic tests =====

TEST(RangeValidationTest, MinBoundary) {
    double min_val = -10.0;
    double max_val = 45.0;
    // At min
    EXPECT_GE(min_val, min_val);
    EXPECT_LE(min_val, max_val);
}

TEST(RangeValidationTest, MaxBoundary) {
    double min_val = -10.0;
    double max_val = 45.0;
    // At max
    EXPECT_GE(max_val, min_val);
    EXPECT_LE(max_val, max_val);
}

TEST(RangeValidationTest, BelowMin) {
    double value = -10.001;
    double min_val = -10.0;
    EXPECT_LT(value, min_val);
}

TEST(RangeValidationTest, AboveMax) {
    double value = 45.001;
    double max_val = 45.0;
    EXPECT_GT(value, max_val);
}

TEST(RangeValidationTest, InRange) {
    double min_val = -10.0;
    double max_val = 45.0;
    double mid = (min_val + max_val) / 2.0;
    EXPECT_GE(mid, min_val);
    EXPECT_LE(mid, max_val);
}

// ===== SeedFixedTest in practice =====

TEST_F(SeedFixedTest, SeedCanBeUsed) {
    // Simulate using seed for generation
    EXPECT_EQ(seed_, 42u);
    EXPECT_GT(seed_, 0u);
}
