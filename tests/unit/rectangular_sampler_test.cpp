#include <gtest/gtest.h>
#include "engine/physics/rectangular_sampler.h"
#include "schema/schema.h"
#include "parser/ast.h"
#include <arrow/array.h>
#include <arrow/type.h>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::schema;

namespace {
Schema make_sensor_schema() {
    Schema s;
    s.type_name = "sensor";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    ColumnDef press;
    press.name = "pressure";
    press.type = DataType::kFloat;
    press.range_min = 900.0;
    press.range_max = 1100.0;
    s.columns.push_back(press);
    ColumnDef status;
    status.name = "status";
    status.type = DataType::kEnum;
    status.enum_values = {"normal", "warning", "fault"};
    s.columns.push_back(status);
    return s;
}
}  // namespace

TEST(RectangularSamplerTest, GenerateUniform) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);
    EXPECT_EQ(result.value().data->num_rows(), 100);
    EXPECT_EQ(result.value().data->num_columns(), 3);
}

TEST(RectangularSamplerTest, GenerateGaussian) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "gaussian", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);
    EXPECT_EQ(result.value().stats.distribution_used, "gaussian");
}

TEST(RectangularSamplerTest, NoConstraintsUseSchemaDefaults) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 1000, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 1000);

    auto table = result.value().data;
    auto temp_col = std::static_pointer_cast<arrow::DoubleArray>(table->column(0)->chunk(0));
    for (int64_t i = 0; i < temp_col->length(); i++) {
        EXPECT_GE(temp_col->Value(i), -50.0);
        EXPECT_LE(temp_col->Value(i), 80.0);
    }
}

TEST(RectangularSamplerTest, WithConstraints) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0},
    };
    GenerationRequest req{schema, constraints, 500, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto table = result.value().data;
    auto temp_col = std::static_pointer_cast<arrow::DoubleArray>(table->column(0)->chunk(0));
    for (int64_t i = 0; i < temp_col->length(); i++) {
        EXPECT_GE(temp_col->Value(i), -10.0);
        EXPECT_LE(temp_col->Value(i), 45.0);
    }
}

TEST(RectangularSamplerTest, MultipleBatches) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 2500, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 2500);
    EXPECT_EQ(result.value().stats.batch_count, 3);
    EXPECT_EQ(result.value().data->num_rows(), 2500);
}

TEST(RectangularSamplerTest, LimitZero) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 0, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 0);
}

TEST(RectangularSamplerTest, LimitOne) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 1, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 1);
    EXPECT_EQ(result.value().data->num_rows(), 1);
}

TEST(RectangularSamplerTest, LimitNegative) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, -1, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RectangularSamplerTest, InvalidDistribution) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "beta", 1000};
    auto result = sampler.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

TEST(RectangularSamplerTest, Determinism) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 1000};
    auto r1 = sampler.generate(req);
    auto r2 = sampler.generate(req);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    auto t1 = r1.value().data;
    auto t2 = r2.value().data;
    auto col1 = std::static_pointer_cast<arrow::DoubleArray>(t1->column(0)->chunk(0));
    auto col2 = std::static_pointer_cast<arrow::DoubleArray>(t2->column(0)->chunk(0));
    for (int64_t i = 0; i < col1->length(); i++) {
        EXPECT_DOUBLE_EQ(col1->Value(i), col2->Value(i))
            << "Mismatch at row " << i;
    }
}

TEST(RectangularSamplerTest, DifferentSeedDifferentOutput) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req1{schema, {}, 100, 42, "uniform", 1000};
    GenerationRequest req2{schema, {}, 100, 43, "uniform", 1000};
    auto r1 = sampler.generate(req1);
    auto r2 = sampler.generate(req2);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    auto col1 = std::static_pointer_cast<arrow::DoubleArray>(
        r1.value().data->column(0)->chunk(0));
    auto col2 = std::static_pointer_cast<arrow::DoubleArray>(
        r2.value().data->column(0)->chunk(0));
    bool different = false;
    for (int64_t i = 0; i < col1->length(); i++) {
        if (col1->Value(i) != col2->Value(i)) { different = true; break; }
    }
    EXPECT_TRUE(different);
}

TEST(RectangularSamplerTest, EnumColumnValues) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok());
    auto status_col = std::static_pointer_cast<arrow::StringArray>(
        result.value().data->column(2)->chunk(0));
    std::set<std::string> seen;
    for (int64_t i = 0; i < status_col->length(); i++) {
        auto val = status_col->GetString(i);
        EXPECT_TRUE(val == "normal" || val == "warning" || val == "fault")
            << "Unexpected enum value: " << val;
        seen.insert(val);
    }
    // With 100 samples, should see at least 2 values
    EXPECT_GE(seen.size(), 2u);
}

TEST(RectangularSamplerTest, ExclusionRateZero) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.value().stats.exclusion_rate, 0.0);
}
