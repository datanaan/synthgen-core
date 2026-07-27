#include <gtest/gtest.h>
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "schema/schema_builder.h"
#include "parser/parser.h"

using namespace synthgen::schema;

class SchemaTest : public ::testing::Test {
protected:
    synthgen::ColumnDef make_col(const std::string& name, synthgen::DataType type,
                                  double min = 0, double max = 0,
                                  bool has_range = false) {
        synthgen::ColumnDef col;
        col.name = name;
        col.type = type;
        if (has_range) {
            col.range_min = min;
            col.range_max = max;
        }
        return col;
    }
};

TEST_F(SchemaTest, ValidSchema) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat, -10.0, 10.0, true));
    s.columns.push_back(make_col("y", synthgen::DataType::kInt));
    auto result = s.validate();
    EXPECT_TRUE(result.ok());
}

TEST_F(SchemaTest, EmptyTypeName) {
    Schema s;
    s.type_name = "";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidSchema);
}

TEST_F(SchemaTest, EmptyColumns) {
    Schema s;
    s.type_name = "test";
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
}

TEST_F(SchemaTest, DuplicateColumnNames) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    s.columns.push_back(make_col("x", synthgen::DataType::kInt));
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kDuplicateColumnName);
}

TEST_F(SchemaTest, InvalidRange) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat, 10.0, 5.0, true));
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidRange);
}

TEST_F(SchemaTest, EqualRangeInvalid) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat, 5.0, 5.0, true));
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
}

TEST_F(SchemaTest, EmptyEnumValues) {
    synthgen::ColumnDef col;
    col.name = "status";
    col.type = synthgen::DataType::kEnum;
    // enum_values is empty
    Schema s;
    s.type_name = "test";
    s.columns.push_back(col);
    auto result = s.validate();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kInvalidEnum);
}

TEST_F(SchemaTest, ValidEnum) {
    synthgen::ColumnDef col;
    col.name = "status";
    col.type = synthgen::DataType::kEnum;
    col.enum_values = {"a", "b", "c"};
    Schema s;
    s.type_name = "test";
    s.columns.push_back(col);
    auto result = s.validate();
    EXPECT_TRUE(result.ok());
}

TEST_F(SchemaTest, FindColumn) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    s.columns.push_back(make_col("y", synthgen::DataType::kInt));
    auto found = s.find_column("x");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "x");
    EXPECT_FALSE(s.find_column("z").has_value());
}

TEST_F(SchemaTest, ColumnIndex) {
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("a", synthgen::DataType::kFloat));
    s.columns.push_back(make_col("b", synthgen::DataType::kInt));
    EXPECT_EQ(s.column_index("a"), 0);
    EXPECT_EQ(s.column_index("b"), 1);
    EXPECT_EQ(s.column_index("z"), -1);
}

TEST_F(SchemaTest, OrderColumns) {
    Schema s;
    s.type_name = "test";
    auto col1 = make_col("ts", synthgen::DataType::kDatetime);
    col1.is_order = true;
    s.columns.push_back(col1);
    s.columns.push_back(make_col("val", synthgen::DataType::kFloat));
    auto oc = s.order_columns();
    ASSERT_EQ(oc.size(), 1u);
    EXPECT_EQ(oc[0], "ts");
}

// Registry tests

TEST_F(SchemaTest, RegistryRegisterAndGet) {
    SchemaRegistry reg;
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    auto r1 = reg.register_schema(std::move(s));
    EXPECT_TRUE(r1.ok());
    auto r2 = reg.get_schema("test");
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(r2.value()->type_name, "test");
}

TEST_F(SchemaTest, RegistryDuplicate) {
    SchemaRegistry reg;
    Schema s1, s2;
    s1.type_name = "test"; s1.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    s2.type_name = "test"; s2.columns.push_back(make_col("y", synthgen::DataType::kInt));
    EXPECT_TRUE(reg.register_schema(std::move(s1)).ok());
    auto r = reg.register_schema(std::move(s2));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, synthgen::ErrorCode::kDuplicateTypeName);
}

TEST_F(SchemaTest, RegistryNotFound) {
    SchemaRegistry reg;
    auto r = reg.get_schema("nonexistent");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, synthgen::ErrorCode::kNotFound);
}

TEST_F(SchemaTest, RegistryHasSchema) {
    SchemaRegistry reg;
    EXPECT_FALSE(reg.has_schema("test"));
    Schema s;
    s.type_name = "test";
    s.columns.push_back(make_col("x", synthgen::DataType::kFloat));
    reg.register_schema(std::move(s));
    EXPECT_TRUE(reg.has_schema("test"));
}

// SchemaBuilder tests

TEST_F(SchemaTest, BuilderBasic) {
    synthgen::parser::Parser parser;
    auto parse_result = parser.parse("DEFINE TYPE sensor { temp: FLOAT [-10.0, 45.0] };");
    ASSERT_TRUE(parse_result.ok());
    ASSERT_TRUE(parse_result.value().errors.empty());
    auto* stmt = std::get_if<synthgen::parser::ast::DefineTypeStmt>(
        &parse_result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);

    SchemaBuilder builder;
    auto schema_result = builder.build(*stmt);
    ASSERT_TRUE(schema_result.ok());
    EXPECT_EQ(schema_result.value().type_name, "sensor");
    ASSERT_EQ(schema_result.value().columns.size(), 1u);
    EXPECT_DOUBLE_EQ(schema_result.value().columns[0].range_min.value(), -10.0);
    EXPECT_DOUBLE_EQ(schema_result.value().columns[0].range_max.value(), 45.0);
}

TEST_F(SchemaTest, BuilderWithEnumAndOrder) {
    synthgen::parser::Parser parser;
    auto parse_result = parser.parse(
        "DEFINE TYPE sensor { ts: DATETIME NOT NULL ORDER, s: ENUM('a', 'b') };");
    ASSERT_TRUE(parse_result.ok());
    ASSERT_TRUE(parse_result.value().errors.empty());
    auto* stmt = std::get_if<synthgen::parser::ast::DefineTypeStmt>(
        &parse_result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);

    SchemaBuilder builder;
    auto schema_result = builder.build(*stmt);
    ASSERT_TRUE(schema_result.ok());
    EXPECT_TRUE(schema_result.value().columns[0].not_null);
    EXPECT_TRUE(schema_result.value().columns[0].is_order);
    ASSERT_EQ(schema_result.value().columns[1].enum_values.size(), 2u);
}
