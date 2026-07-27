#include <gtest/gtest.h>
#include <string>
#include "parser/parser.h"
#include "parser/lexer.h"
#include "common/result.h"

using namespace synthgen;
using namespace synthgen::parser;

// Helper: parse source and return result
static Result<ParseResult> parse(const std::string& src) {
    Parser p;
    return p.parse(src);
}

// Helper: tokenize source and return result
static Result<std::vector<Token>> lex(const std::string& src) {
    Lexer l(src);
    return l.tokenize();
}

// =============================================================================
// TEST GROUP 1: Empty / Whitespace / Semicolons
// =============================================================================

// Test 1a: Empty string
TEST(ParserFuzzTest, EmptyString) {
    auto result = parse("");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().program.statements.empty());
    EXPECT_TRUE(result.value().errors.empty());
}

// Test 1b: Whitespace-only
TEST(ParserFuzzTest, WhitespaceOnly) {
    auto result = parse("   \t\n\r\n  \n  ");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().program.statements.empty());
    EXPECT_TRUE(result.value().errors.empty());
}

// Test 1c: Single semicolon
TEST(ParserFuzzTest, SingleSemicolon) {
    auto result = parse(";");
    ASSERT_TRUE(result.ok());
    // A bare semicolon is not a valid statement; expect a parse error
    EXPECT_FALSE(result.value().errors.empty())
        << "Bare semicolon should produce a parse error";
}

// Test 1d: Multiple semicolons
TEST(ParserFuzzTest, MultipleSemicolons) {
    auto result = parse(";;; ; ;;");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Multiple bare semicolons should produce parse errors";
}

// =============================================================================
// TEST 2: DEFINE TYPE with 0 columns
// =============================================================================

TEST(ParserFuzzTest, DefineTypeZeroColumns) {
    auto result = parse("DEFINE TYPE empty {};");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "TYPE with 0 columns should produce an error";
    // The do-while loop always calls parse_column_def at least once,
    // so the error comes from expect(L_IDENT) failing when it sees '}'.
    bool found_error = false;
    for (const auto& e : result.value().errors) {
        if (e.message.find("column name") != std::string::npos ||
            e.message.find("Expected") != std::string::npos ||
            e.message.find("column") != std::string::npos) {
            found_error = true;
        }
    }
    EXPECT_TRUE(found_error) << "Expected error about columns. Got: "
        << (result.value().errors.empty() ? "(no errors)" : result.value().errors[0].message);
}

// =============================================================================
// TEST 3: DEFINE TYPE with keyword-named columns
// =============================================================================

TEST(ParserFuzzTest, DefineTypeKeywordColumnNames) {
    // Column named "DEFINE" — lexer will produce K_DEFINE, not L_IDENT
    // Parser expects L_IDENT for column name, so this should fail at parse time
    auto result = parse("DEFINE TYPE kw_test { DEFINE: FLOAT };");
    ASSERT_TRUE(result.ok());
    // Should have errors because DEFINE is a keyword token, not an identifier
    EXPECT_FALSE(result.value().errors.empty())
        << "Column named after keyword DEFINE should produce a parse error";
}

// =============================================================================
// TEST 4: DEFINE TYPE with duplicate column names
// =============================================================================

TEST(ParserFuzzTest, DefineTypeDuplicateColumnNames) {
    auto result = parse("DEFINE TYPE dup { x: FLOAT, x: INT };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Duplicate column names should produce an error";
    bool found_dup = false;
    for (const auto& e : result.value().errors) {
        if (e.message.find("Duplicate column") != std::string::npos) {
            found_dup = true;
        }
    }
    EXPECT_TRUE(found_dup) << "Expected 'Duplicate column' error message";
}

// =============================================================================
// TEST 5: DEFINE TYPE with inverted FLOAT range (min > max)
// =============================================================================

TEST(ParserFuzzTest, DefineTypeInvertedFloatRange) {
    auto result = parse("DEFINE TYPE bad_range { temp: FLOAT [80.0, -50.0] };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Inverted range [80.0, -50.0] should produce an error";
    bool found_range_err = false;
    for (const auto& e : result.value().errors) {
        if (e.message.find("range_min must be less than range_max") != std::string::npos) {
            found_range_err = true;
        }
    }
    EXPECT_TRUE(found_range_err) << "Expected range_min < range_max error";
}

// =============================================================================
// TEST 6: DEFINE TYPE with extremely long type name (1000 chars)
// =============================================================================

TEST(ParserFuzzTest, DefineTypeExtremelyLongName) {
    std::string long_name(1000, 'A');
    std::string src = "DEFINE TYPE " + long_name + " { x: FLOAT };";
    auto result = parse(src);
    ASSERT_TRUE(result.ok()) << "Parser should not crash on long names";
    EXPECT_TRUE(result.value().errors.empty())
        << "A very long valid name should parse successfully";
    ASSERT_EQ(result.value().program.statements.size(), 1u);
    auto* stmt = std::get_if<ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name.size(), 1000u);
}

// =============================================================================
// TEST 7: DEFINE TYPE with special characters in column names
// =============================================================================

TEST(ParserFuzzTest, DefineTypeSpecialCharColumnNames) {
    // Emoji column name - lexer won't recognize emoji as identifier start
    auto result1 = parse("DEFINE TYPE emoji { \xF0\x9F\x98\x80: FLOAT };");
    ASSERT_TRUE(result1.ok());  // parse() always returns ok()
    EXPECT_FALSE(result1.value().errors.empty())
        << "Emoji in column name should cause a lex/parse error";

    // Column with spaces - lexer will see separate tokens
    auto result2 = parse("DEFINE TYPE space { hello world: FLOAT };");
    ASSERT_TRUE(result2.ok());
    EXPECT_FALSE(result2.value().errors.empty())
        << "Column name with space should cause a parse error";

    // Unicode (non-ASCII) in identifier -- lexer uses isalnum which is locale-dependent
    // but standard C isalnum is only for [a-zA-Z0-9]
    auto result3 = parse("DEFINE TYPE uni { col\xC3\xA9: FLOAT };");
    // 0xC3 is not alphanumeric in C locale, so lexer should produce error
    ASSERT_TRUE(result3.ok());
    EXPECT_FALSE(result3.value().errors.empty())
        << "Non-ASCII unicode in column name should cause an error";
}

// =============================================================================
// TEST 8: ENUM with 0 values and ENUM with 100 values
// =============================================================================

TEST(ParserFuzzTest, EnumZeroValues) {
    auto result = parse("DEFINE TYPE empty_enum { s: ENUM() };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "ENUM with 0 values should produce an error";
}

TEST(ParserFuzzTest, EnumManyValues) {
    std::string src = "DEFINE TYPE big_enum { s: ENUM(";
    for (int i = 0; i < 100; i++) {
        if (i > 0) src += ", ";
        src += "'val_" + std::to_string(i) + "'";
    }
    src += ") };";
    auto result = parse(src);
    ASSERT_TRUE(result.ok()) << "Parser should handle ENUM with 100 values";
    EXPECT_TRUE(result.value().errors.empty())
        << "ENUM with 100 values should be valid";
    auto* stmt = std::get_if<ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->columns[0].enum_values.size(), 100u);
}

// =============================================================================
// TEST 9: CONSTRAINT referencing nonexistent column
// =============================================================================

TEST(ParserFuzzTest, ConstraintNonexistentColumn) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { nonexistent BETWEEN 0 AND 10 };");
    ASSERT_TRUE(result.ok());
    // Parser currently does NOT validate column existence in constraints at parse time.
    // This is a known limitation — the constraint will parse but reference a column
    // that doesn't exist in the type. Downstream validation should catch this.
    auto& stmts = result.value().program.statements;
    ASSERT_EQ(stmts.size(), 2u);
    auto* con = std::get_if<ast::DefineConstraintStmt>(&stmts[1]);
    ASSERT_NE(con, nullptr);
    EXPECT_EQ(con->items[0].column_name, "nonexistent");
    // NOTE: This test documents the behavior. If the parser is later enhanced to
    // validate column references, this assertion should be updated.
}

// =============================================================================
// TEST 10: CONSTRAINT with nonsensical values (very large, scientific notation)
// =============================================================================

TEST(ParserFuzzTest, ConstraintNonsensicalValues) {
    // Very large values (but valid syntax)
    auto result1 = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN -1e308 AND 1e308 };");
    ASSERT_TRUE(result1.ok()) << "Scientific notation bounds should parse";
    EXPECT_TRUE(result1.value().errors.empty());
    auto* con1 = std::get_if<ast::DefineConstraintStmt>(
        &result1.value().program.statements[1]);
    ASSERT_NE(con1, nullptr);
    EXPECT_LT(con1->items[0].value_min, -1e300);
    EXPECT_GT(con1->items[0].value_max, 1e300);

    // Equal bounds (min == max)
    auto result2 = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c2 ON t { x BETWEEN 5.0 AND 5.0 };");
    ASSERT_TRUE(result2.ok());
    EXPECT_TRUE(result2.value().errors.empty())
        << "BETWEEN with equal bounds is syntactically valid (semantic check is downstream)";
}

// =============================================================================
// TEST 11: LOAD DATA with various paths
// =============================================================================

TEST(ParserFuzzTest, LoadDataVariousPaths) {
    // Path with special characters (valid string literal)
    auto result1 = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "LOAD DATA INTO t FROM '/path/with spaces/and-dashes/file.parquet';");
    ASSERT_TRUE(result1.ok());
    EXPECT_TRUE(result1.value().errors.empty());

    // Empty path string — parser accepts any string
    auto result2 = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "LOAD DATA INTO t FROM '';");
    ASSERT_TRUE(result2.ok());
    EXPECT_TRUE(result2.value().errors.empty())
        << "Empty path string is syntactically valid (runtime should reject)";
    auto* stmt2 = std::get_if<ast::LoadDataStmt>(&result2.value().program.statements[1]);
    ASSERT_NE(stmt2, nullptr);
    EXPECT_EQ(stmt2->file_path, "");
}

// =============================================================================
// TEST 12: Multiple statements with one intentionally broken in the middle
// =============================================================================

TEST(ParserFuzzTest, BrokenStatementInMiddle) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "BROKEN GARBAGE HERE;"
        "DEFINE TYPE t2 { y: INT };");
    ASSERT_TRUE(result.ok());
    auto& stmts = result.value().program.statements;
    auto& errors = result.value().errors;

    // The broken statement should produce an error
    EXPECT_FALSE(errors.empty())
        << "Broken statement should produce at least one parse error";

    // But the valid statements before and after should still parse
    // The parser should recover and parse DEFINE TYPE t2
    bool found_t = false, found_t2 = false;
    for (auto& s : stmts) {
        if (auto* dt = std::get_if<ast::DefineTypeStmt>(&s)) {
            if (dt->type_name == "t") found_t = true;
            if (dt->type_name == "t2") found_t2 = true;
        }
    }
    EXPECT_TRUE(found_t) << "First valid DEFINE TYPE should parse";
    EXPECT_TRUE(found_t2) << "Parser error recovery should reach second valid DEFINE TYPE";
}

// =============================================================================
// TEST 13: GENERATE TABLE with limit=0, limit=-1, limit=999999999
// =============================================================================

TEST(ParserFuzzTest, GenerateTableLimitZero) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE out FROM t WITH CONSTRAINTS c LIMIT 0;");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().errors.empty());
    auto* stmt = std::get_if<ast::GenerateTableStmt>(
        &result.value().program.statements[2]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->limit, 0);
}

TEST(ParserFuzzTest, GenerateTableLimitNegative) {
    // limit=-1 — parser uses S_MINUS before L_INT, so "-1" is two tokens
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE out FROM t WITH CONSTRAINTS c LIMIT -1;");
    ASSERT_TRUE(result.ok());
    // The parser expects L_INT after LIMIT; getting S_MINUS should cause an error
    EXPECT_FALSE(result.value().errors.empty())
        << "LIMIT -1 should produce a parse error (negative limit)";
}

TEST(ParserFuzzTest, GenerateTableLimitHuge) {
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE out FROM t WITH CONSTRAINTS c LIMIT 999999999;");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().errors.empty());
    auto* stmt = std::get_if<ast::GenerateTableStmt>(
        &result.value().program.statements[2]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->limit, 999999999);
}

// =============================================================================
// TEST 14: Comments everywhere — verify they're ignored
// =============================================================================

TEST(ParserFuzzTest, CommentsEverywhere) {
    auto result = parse(
        "-- top-level comment\n"
        "DEFINE -- comment after keyword\n"
        "TYPE -- comment after TYPE\n"
        "commented_type {\n"
        "  -- comment inside type block\n"
        "  x: FLOAT, -- inline comment after column\n"
        "  y: INT -- comment before closing brace\n"
        "};\n"
        "-- trailing comment");
    ASSERT_TRUE(result.ok()) << "Comments should not break parsing";
    EXPECT_TRUE(result.value().errors.empty())
        << "Comments should be silently ignored";
    ASSERT_EQ(result.value().program.statements.size(), 1u);
    auto* stmt = std::get_if<ast::DefineTypeStmt>(
        &result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name, "commented_type");
    ASSERT_EQ(stmt->columns.size(), 2u);
    EXPECT_EQ(stmt->columns[0].name, "x");
    EXPECT_EQ(stmt->columns[1].name, "y");
}

// =============================================================================
// TEST 15: Deeply nested / malformed syntax
// =============================================================================

TEST(ParserFuzzTest, UnclosedBraces) {
    auto result = parse("DEFINE TYPE unclosed { x: FLOAT;");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Unclosed brace should produce a parse error";
}

TEST(ParserFuzzTest, MissingSemicolon) {
    // Missing semicolon after type def — parser should still work
    // (semicolon is optional per the parser code)
    auto result = parse("DEFINE TYPE nosemi { x: FLOAT }");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().errors.empty())
        << "Missing semicolon after type def should be OK (semicolon is optional)";
    ASSERT_EQ(result.value().program.statements.size(), 1u);
}

TEST(ParserFuzzTest, ExtraClosingBrace) {
    auto result = parse("DEFINE TYPE t { x: FLOAT }};");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Extra closing brace should produce a parse error";
}

TEST(ParserFuzzTest, DeeplyNestedBrackets) {
    // The parser doesn't support nested brackets, but let's see behavior
    auto result = parse("DEFINE TYPE t { x: FLOAT [[1.0, 2.0]] };");
    ASSERT_TRUE(result.ok());
    // Should fail — nested brackets are not valid syntax
    EXPECT_FALSE(result.value().errors.empty())
        << "Deeply nested brackets should produce an error";
}

TEST(ParserFuzzTest, CompletelyGarbledInput) {
    auto result = parse("{{{}}}@@@###!!!..///");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Complete garbage should produce parse errors";
    EXPECT_TRUE(result.value().program.statements.empty())
        << "No statements should be produced from garbage";
}

// =============================================================================
// Additional edge cases discovered during analysis
// =============================================================================

TEST(ParserFuzzTest, LexerMinusSignHandling) {
    // Verify that "-" followed by digit produces a number, not S_MINUS + L_INT
    // The lexer has special handling: if '-' is followed by digit, scan_number
    auto result = lex("-42");
    ASSERT_TRUE(result.ok());
    auto& tokens = result.value();
    // The lexer scans "-42" as a number token
    EXPECT_EQ(tokens[0].type, TokenType::L_INT);
    EXPECT_EQ(tokens[0].lexeme, "-42");
}

TEST(ParserFuzzTest, LexerNegativeFloat) {
    auto result = lex("-3.14");
    ASSERT_TRUE(result.ok());
    auto& tokens = result.value();
    EXPECT_EQ(tokens[0].type, TokenType::L_FLOAT);
    EXPECT_EQ(tokens[0].lexeme, "-3.14");
}

TEST(ParserFuzzTest, LexerMinusThenNonDigit) {
    // "-abc" should produce S_MINUS then L_IDENT
    auto result = lex("-abc");
    ASSERT_TRUE(result.ok());
    auto& tokens = result.value();
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::S_MINUS);
    EXPECT_EQ(tokens[1].type, TokenType::L_IDENT);
    EXPECT_EQ(tokens[1].lexeme, "abc");
}

TEST(ParserFuzzTest, MultipleValidStatementsNoErrors) {
    auto result = parse(
        "DEFINE TYPE sensor { ts: DATETIME NOT NULL ORDER, temp: FLOAT [-50.0, 80.0] };"
        "DEFINE CONSTRAINT safe ON sensor { temp BETWEEN -40 AND 60 };"
        "GENERATE TABLE output FROM sensor WITH CONSTRAINTS safe LIMIT 100;");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().errors.empty())
        << "Valid multi-statement input should have no errors";
    ASSERT_EQ(result.value().program.statements.size(), 3u);
}

TEST(ParserFuzzTest, TypeNamedAsKeyword) {
    // Naming a type "FLOAT" — lexer produces K_FLOAT, parser expects L_IDENT
    auto result = parse("DEFINE TYPE FLOAT { x: INT };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Type named FLOAT (keyword) should produce a parse error";
}

TEST(ParserFuzzTest, ConstraintNameIsKeyword) {
    // Constraint named "DEFINE" — lexer produces K_DEFINE, parser expects L_IDENT
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT DEFINE ON t { x BETWEEN 0 AND 1 };");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Constraint named DEFINE (keyword) should produce a parse error";
}

TEST(ParserFuzzTest, TableNamedAsKeyword) {
    // Table named "LOAD" in GENERATE TABLE — lexer produces K_LOAD
    auto result = parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };"
        "GENERATE TABLE LOAD FROM t WITH CONSTRAINTS c LIMIT 10;");
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().errors.empty())
        << "Table named LOAD (keyword) should produce a parse error";
}
