#include <gtest/gtest.h>
#include "parser/parser.h"
#include "common/result.h"

using namespace synthgen;
using namespace synthgen::parser;

class ParserTest : public ::testing::Test {
protected:
    Result<ParseResult> parse(const std::string& src) {
        Parser p;
        return p.parse(src);
    }
};

TEST_F(ParserTest, DefineTypeBasic) {
    auto result = parse(
        "DEFINE TYPE sensor {"
        "  timestamp: DATETIME NOT NULL ORDER,"
        "  temperature: FLOAT [-50.0, 80.0],"
        "  status: ENUM('normal', 'warning', 'fault')"
        "};");
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto& prog = result.value().program;
    ASSERT_EQ(prog.statements.size(), 1u);
    auto* stmt = std::get_if<ast::DefineTypeStmt>(&prog.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name, "sensor");
    ASSERT_EQ(stmt->columns.size(), 3u);
    EXPECT_EQ(stmt->columns[0].name, "timestamp");
    EXPECT_EQ(stmt->columns[0].type, synthgen::DataType::kDatetime);
    EXPECT_TRUE(stmt->columns[0].not_null);
    EXPECT_TRUE(stmt->columns[0].is_order);
    EXPECT_EQ(stmt->columns[1].name, "temperature");
    EXPECT_EQ(stmt->columns[1].type, synthgen::DataType::kFloat);
    EXPECT_DOUBLE_EQ(stmt->columns[1].range_min.value(), -50.0);
    EXPECT_DOUBLE_EQ(stmt->columns[1].range_max.value(), 80.0);
    EXPECT_EQ(stmt->columns[2].name, "status");
    EXPECT_EQ(stmt->columns[2].type, synthgen::DataType::kEnum);
    ASSERT_EQ(stmt->columns[2].enum_values.size(), 3u);
    EXPECT_EQ(stmt->columns[2].enum_values[0], "normal");
}

TEST_F(ParserTest, DefineTypeNoRange) {
    auto result = parse("DEFINE TYPE simple { x: FLOAT };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_FALSE(stmt->columns[0].range_min.has_value());
}

TEST_F(ParserTest, DefineTypeDuplicateColumn) {
    auto result = parse("DEFINE TYPE dup { x: FLOAT, x: INT };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}

TEST_F(ParserTest, DefineTypeInvalidRange) {
    auto result = parse("DEFINE TYPE bad { x: FLOAT [10.0, 5.0] };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}

TEST_F(ParserTest, DefineTypeEnumEmpty) {
    auto result = parse("DEFINE TYPE bad { s: ENUM() };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}

TEST_F(ParserTest, LoadData) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "LOAD DATA INTO t FROM '/data/file.parquet';");
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.value().program.statements.size(), 2u);
    auto* stmt = std::get_if<ast::LoadDataStmt>(&result.value().program.statements[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name, "t");
    EXPECT_EQ(stmt->file_path, "/data/file.parquet");
}

TEST_F(ParserTest, LoadDataUndefinedType) {
    auto result = parse("LOAD DATA INTO nonexistent FROM '/data/file.parquet';");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}

TEST_F(ParserTest, DefineConstraint) {
    auto result = parse(
        "DEFINE TYPE t { temp: FLOAT, press: FLOAT };"
        "DEFINE CONSTRAINT safe ON t { temp BETWEEN -10 AND 45, press > 900 };");
    ASSERT_TRUE(result.ok());
    auto& stmts = result.value().program.statements;
    auto* stmt = std::get_if<ast::DefineConstraintStmt>(&stmts[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->constraint_name, "safe");
    EXPECT_EQ(stmt->type_name, "t");
    ASSERT_EQ(stmt->items.size(), 2u);
    EXPECT_EQ(stmt->items[0].column_name, "temp");
    EXPECT_EQ(stmt->items[0].op, ast::ConstraintOperator::kBetween);
    EXPECT_DOUBLE_EQ(stmt->items[0].value_min, -10.0);
    EXPECT_DOUBLE_EQ(stmt->items[0].value_max, 45.0);
    EXPECT_EQ(stmt->items[1].column_name, "press");
    EXPECT_EQ(stmt->items[1].op, ast::ConstraintOperator::kGreaterThan);
    EXPECT_DOUBLE_EQ(stmt->items[1].value_min, 900.0);
}

TEST_F(ParserTest, DefineConstraintLessThan) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x < 100 };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<ast::DefineConstraintStmt>(
        &result.value().program.statements[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->items[0].op, ast::ConstraintOperator::kLessThan);
    EXPECT_DOUBLE_EQ(stmt->items[0].value_max, 100.0);
}

TEST_F(ParserTest, DefineConstraintGeLe) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x >= 0, x <= 100 };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<ast::DefineConstraintStmt>(
        &result.value().program.statements[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->items[0].op, ast::ConstraintOperator::kGreaterEqual);
    EXPECT_EQ(stmt->items[1].op, ast::ConstraintOperator::kLessEqual);
}

TEST_F(ParserTest, DefineConstraintUndefinedType) {
    auto result = parse("DEFINE CONSTRAINT c ON nonexistent { x BETWEEN 0 AND 1 };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}

TEST_F(ParserTest, GenerateTable) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE output FROM t WITH CONSTRAINTS c LIMIT 1000;");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<ast::GenerateTableStmt>(
        &result.value().program.statements[2]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->table_name, "output");
    EXPECT_EQ(stmt->type_name, "t");
    EXPECT_EQ(stmt->constraint_name, "c");
    EXPECT_EQ(stmt->limit, 1000);
}

TEST_F(ParserTest, GenerateTableLimitZero) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE output FROM t WITH CONSTRAINTS c LIMIT 0;");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<ast::GenerateTableStmt>(
        &result.value().program.statements[2]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->limit, 0);
}

TEST_F(ParserTest, DuringUnsupported) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT, s: ENUM('a', 'b') };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 DURING s = 'a' };");
    ASSERT_TRUE(result.ok());
    bool found_unsupported = false;
    for (auto& e : result.value().errors) {
        if (e.code == ParseErrorCode::kUnsupportedInV1) found_unsupported = true;
    }
    EXPECT_TRUE(found_unsupported);
}

TEST_F(ParserTest, WhenUnsupported) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT, y: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x > 0 WHEN y > 5 };");
    ASSERT_TRUE(result.ok());
    bool found = false;
    for (auto& e : result.value().errors) {
        if (e.code == ParseErrorCode::kUnsupportedInV1 &&
            e.message.find("WHEN") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, AggregateUnsupported) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { AVG(x) OVER (INTERVAL 1 HOUR) <= 40 };");
    ASSERT_TRUE(result.ok());
    bool found = false;
    for (auto& e : result.value().errors) {
        if (e.code == ParseErrorCode::kUnsupportedInV1 &&
            e.message.find("Aggregate") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, EmptyInput) {
    auto result = parse("");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().program.statements.empty());
    EXPECT_TRUE(result.value().errors.empty());
}

TEST_F(ParserTest, SyntaxError) {
    auto result = parse("}}}");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty());
}
