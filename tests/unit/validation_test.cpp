#include <gtest/gtest.h>
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "schema/schema.h"
#include "engine/physics/rectangular_sampler.h"

#include <arrow/api.h>
#include <arrow/builder.h>

using namespace synthgen;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::physics;

namespace {

schema::Schema make_test_schema() {
    schema::Schema s;
    s.type_name = "test";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    return s;
}

std::shared_ptr<arrow::Table> make_batch(const std::vector<double>& temps) {
    arrow::DoubleBuilder builder;
    for (auto v : temps) builder.Append(v);
    std::shared_ptr<arrow::Array> arr;
    builder.Finish(&arr);
    return arrow::Table::Make(
        arrow::schema({arrow::field("temperature", arrow::float64())}),
        {arr});
}

std::vector<parser::ast::ConstraintItem> make_constraint() {
    return {{"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0}};
}

}  // namespace

TEST(ValidationTest, AllPass) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({0.0, 10.0, 20.0, 30.0, 40.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_checked, 5);
    EXPECT_EQ(result.value().rows_passed, 5);
    EXPECT_EQ(result.value().rows_failed, 0);
    EXPECT_DOUBLE_EQ(result.value().pass_rate, 1.0);
}

TEST(ValidationTest, SomeFail) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({-10.0, 0.0, 50.0, 100.0, -20.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 3);
    EXPECT_EQ(result.value().failures.size(), 3u);
    EXPECT_DOUBLE_EQ(result.value().pass_rate, 0.4);
}

TEST(ValidationTest, EmptyBatch) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_checked, 0);
    EXPECT_DOUBLE_EQ(result.value().pass_rate, 1.0);
}

TEST(ValidationTest, NoConstraints) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, {});
    auto batch = make_batch({999.0, -999.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 2);
    EXPECT_EQ(result.value().rows_failed, 0);
}

TEST(ValidationTest, BoundaryMinPass) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({-10.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 0);
}

TEST(ValidationTest, BoundaryMaxPass) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({45.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 0);
}

TEST(ValidationTest, BelowMinFail) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({-10.001});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 1);
}

TEST(ValidationTest, AboveMaxFail) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto batch = make_batch({45.001});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 1);
}

TEST(ValidationTest, Max100Failures) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    std::vector<double> temps(200, 100.0);  // all out of range
    auto batch = make_batch(temps);
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_failed, 200);
    EXPECT_EQ(result.value().failures.size(), 100u);  // max 100
}

TEST(ValidationTest, Explain) {
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());
    auto info = validator.explain();
    EXPECT_EQ(info.path, "value_range_validation");
    EXPECT_EQ(info.constraint_classification.value_range, 1);
}

TEST(ValidationTest, ColumnNotInBatch) {
    // Schema has "temperature", but batch has a different column
    auto schema = make_test_schema();
    ValueRangeValidator validator(schema, make_constraint());

    // Create batch with a different column name
    arrow::DoubleBuilder builder;
    builder.Append(5.0);
    std::shared_ptr<arrow::Array> arr;
    builder.Finish(&arr);
    auto batch = arrow::Table::Make(
        arrow::schema({arrow::field("other_column", arrow::float64())}),
        {arr});

    auto result = validator.validate_batch(batch);
    EXPECT_FALSE(result.ok());
}

// TailReport tests

TEST(TailReportTest, PurePhysicsPath) {
    auto schema = make_test_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, make_constraint(), 100, 42, "uniform", 1000};
    auto gen_result = sampler.generate(req);
    ASSERT_TRUE(gen_result.ok());

    ValueRangeValidator validator(schema, make_constraint());
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    TailReportBuilder builder;
    auto report = builder.build(gen_result.value(), val_result.value(), req, make_constraint());
    ASSERT_TRUE(report.ok());

    auto& r = report.value();
    EXPECT_EQ(r.epistemological_bias, "physical_first");
    EXPECT_EQ(r.data_grade, "physics_guaranteed");
    EXPECT_EQ(r.rows_generated, 100);
    EXPECT_EQ(r.rows_failed_validation, 0);
    EXPECT_FALSE(r.tail_exclusion_statement.empty());
    EXPECT_EQ(r.distribution_used, "uniform");
    EXPECT_EQ(r.seed_used, 42u);
}

TEST(TailReportTest, ExclusionRateZero) {
    auto schema = make_test_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, make_constraint(), 1000, 42, "uniform", 1000};
    auto gen_result = sampler.generate(req);
    ASSERT_TRUE(gen_result.ok());

    ValueRangeValidator validator(schema, make_constraint());
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    TailReportBuilder builder;
    auto report = builder.build(gen_result.value(), val_result.value(), req, make_constraint());
    ASSERT_TRUE(report.ok());
    EXPECT_DOUBLE_EQ(report.value().total_exclusion_rate, 0.0);
}

TEST(TailReportTest, HonestDeclarations) {
    TailReportV1 report;
    EXPECT_EQ(report.epistemological_bias, "physical_first");
    EXPECT_EQ(report.data_grade, "physics_guaranteed");
    EXPECT_NE(report.tail_exclusion_statement.find("risk"), std::string::npos);
}
