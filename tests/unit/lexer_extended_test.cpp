#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "common/result.h"

using namespace synthgen;
using namespace synthgen::parser;

namespace {

Result<std::vector<Token>> lex(const std::string& src) {
    Lexer l(src);
    return l.tokenize();
}

}  // namespace

// ===== Multiple comments =====

TEST(LexerExtended, MultipleCommentsBetweenTokens) {
    auto result = lex("DEFINE -- c1\n-- c2\n-- c3\nTYPE");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
}

TEST(LexerExtended, CommentAtEndNoNewline) {
    auto result = lex("DEFINE -- trailing");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    ASSERT_EQ(t.size(), 2u);
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::T_EOF);
}

// ===== Mixed whitespace =====

TEST(LexerExtended, MixedWhitespace) {
    auto result = lex("DEFINE\tTYPE  FLOAT");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    ASSERT_GE(t.size(), 4u);
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
    EXPECT_EQ(t[2].type, TokenType::K_FLOAT);
}

TEST(LexerExtended, OnlyWhitespace) {
    auto result = lex("   \t\n  \r\n  ");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].type, TokenType::T_EOF);
}

TEST(LexerExtended, TabSeparatedTokens) {
    auto result = lex("a\tb\tc");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t.size(), 4u);
    EXPECT_EQ(t[0].lexeme, "a");
    EXPECT_EQ(t[1].lexeme, "b");
    EXPECT_EQ(t[2].lexeme, "c");
}

// ===== Scientific notation =====

TEST(LexerExtended, ScientificNotationVariants) {
    auto result = lex("1e10 1E10 1e-10 1e+10");
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto& t = result.value();
    for (size_t i = 0; i < t.size() - 1; ++i) {
        EXPECT_EQ(t[i].type, TokenType::L_FLOAT) << "Token " << i << ": " << t[i].lexeme;
    }
}

// ===== Long identifier =====

TEST(LexerExtended, LongIdentifier) {
    std::string long_id(200, 'a');
    auto result = lex(long_id);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()[0].type, TokenType::L_IDENT);
    EXPECT_EQ(result.value()[0].lexeme, long_id);
}

// ===== String edge cases =====

TEST(LexerExtended, EmptyStringLiteral) {
    auto result = lex("''");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()[0].type, TokenType::L_STRING);
}

TEST(LexerExtended, StringWithSpaces) {
    auto result = lex("'hello world'");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()[0].type, TokenType::L_STRING);
    EXPECT_EQ(result.value()[0].lexeme, "'hello world'");
}

TEST(LexerExtended, MultipleStrings) {
    auto result = lex("'a' 'b' 'c'");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t.size(), 4u);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(t[i].type, TokenType::L_STRING);
    }
}

// ===== Line/column tracking =====

TEST(LexerExtended, LineTrackingWithComments) {
    auto result = lex("a -- comment\nb\nc");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].line, 1);
    EXPECT_EQ(t[1].line, 2);
    EXPECT_EQ(t[2].line, 3);
}

TEST(LexerExtended, ColumnTrackingAfterNewline) {
    auto result = lex("ab\ncd");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].column, 1);
    EXPECT_EQ(t[1].column, 1);
}

// ===== Multiple statements =====

TEST(LexerExtended, MultipleStatements) {
    auto result = lex("DEFINE TYPE x { }; DEFINE TYPE y { };");
    ASSERT_TRUE(result.ok());
    int define_count = 0;
    for (const auto& tok : result.value()) {
        if (tok.type == TokenType::K_DEFINE) define_count++;
    }
    EXPECT_EQ(define_count, 2);
}

// ===== All data type keywords =====

TEST(LexerExtended, AllDataTypeKeywords) {
    auto result = lex("FLOAT INT DATETIME STRING ENUM");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::K_FLOAT);
    EXPECT_EQ(t[1].type, TokenType::K_INT);
    EXPECT_EQ(t[2].type, TokenType::K_DATETIME);
    EXPECT_EQ(t[3].type, TokenType::K_STRING);
    EXPECT_EQ(t[4].type, TokenType::K_ENUM);
}

// ===== Mixed case keywords =====

TEST(LexerExtended, MixedCaseKeywords) {
    auto result = lex("Define Type Float Int");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
    EXPECT_EQ(t[2].type, TokenType::K_FLOAT);
    EXPECT_EQ(t[3].type, TokenType::K_INT);
}

// ===== Identifier with underscores and numbers =====

TEST(LexerExtended, IdentifierWithUnderscoreAndNumbers) {
    auto result = lex("my_type_1 sensor_v2");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()[0].type, TokenType::L_IDENT);
    EXPECT_EQ(result.value()[0].lexeme, "my_type_1");
    EXPECT_EQ(result.value()[1].type, TokenType::L_IDENT);
    EXPECT_EQ(result.value()[1].lexeme, "sensor_v2");
}

// ===== EOF always present =====

TEST(LexerExtended, EOFTokenAlwaysPresent) {
    auto result = lex("DEFINE");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().back().type, TokenType::T_EOF);
}

// ===== Zero literal =====

TEST(LexerExtended, ZeroLiterals) {
    auto result = lex("0 0.0");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value()[0].type, TokenType::L_INT);
    EXPECT_EQ(result.value()[1].type, TokenType::L_FLOAT);
}

// ===== Negative number (minus + number) =====

TEST(LexerExtended, NegativeFloatAsMinusAndNumber) {
    auto result = lex("-3.14");
    ASSERT_TRUE(result.ok());
    EXPECT_GE(result.value().size(), 2u);  // Either - + 3.14 or -3.14
}

TEST(LexerExtended, NegativeIntAsMinusAndNumber) {
    auto result = lex("-42");
    ASSERT_TRUE(result.ok());
    EXPECT_GE(result.value().size(), 2u);
}

// ===== Comma and semicolon in sequence =====

TEST(LexerExtended, SymbolSequence) {
    auto result = lex(",;:,");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::S_COMMA);
    EXPECT_EQ(t[1].type, TokenType::S_SEMICOLON);
    EXPECT_EQ(t[2].type, TokenType::S_COLON);
    EXPECT_EQ(t[3].type, TokenType::S_COMMA);
}

// ===== Parentheses and brackets =====

TEST(LexerExtended, BracketSequence) {
    auto result = lex("([{}])");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::S_LPAREN);
    EXPECT_EQ(t[1].type, TokenType::S_LBRACKET);
    EXPECT_EQ(t[2].type, TokenType::S_LBRACE);
    EXPECT_EQ(t[3].type, TokenType::S_RBRACE);
    EXPECT_EQ(t[4].type, TokenType::S_RBRACKET);
    EXPECT_EQ(t[5].type, TokenType::S_RPAREN);
}

// ===== Full SynthLang statement tokenization =====

TEST(LexerExtended, FullDefineTypeStatement) {
    auto result = lex(
        "DEFINE TYPE sensor_log {\n"
        "  timestamp: DATETIME NOT NULL ORDER,\n"
        "  temperature: FLOAT [-50.0, 80.0],\n"
        "  status: ENUM('normal', 'warning', 'fault')\n"
        "};");
    ASSERT_TRUE(result.ok()) << result.error().message;
    // Verify no errors and has expected structure
    bool has_define = false, has_type = false, has_eof = false;
    for (const auto& tok : result.value()) {
        if (tok.type == TokenType::K_DEFINE) has_define = true;
        if (tok.type == TokenType::K_TYPE) has_type = true;
        if (tok.type == TokenType::T_EOF) has_eof = true;
    }
    EXPECT_TRUE(has_define);
    EXPECT_TRUE(has_type);
    EXPECT_TRUE(has_eof);
}

TEST(LexerExtended, FullConstraintStatement) {
    auto result = lex(
        "DEFINE CONSTRAINT safe_range ON sensor_log {\n"
        "  temperature BETWEEN -10 AND 45,\n"
        "  pressure > 900\n"
        "};");
    ASSERT_TRUE(result.ok()) << result.error().message;
    bool has_between = false, has_gt = false;
    for (const auto& tok : result.value()) {
        if (tok.type == TokenType::K_BETWEEN) has_between = true;
        if (tok.type == TokenType::S_GT) has_gt = true;
    }
    EXPECT_TRUE(has_between);
    EXPECT_TRUE(has_gt);
}

TEST(LexerExtended, GenerateStatement) {
    auto result = lex("GENERATE TABLE output FROM t WITH CONSTRAINTS c LIMIT 1000;");
    ASSERT_TRUE(result.ok()) << result.error().message;
    bool has_generate = false, has_limit = false;
    for (const auto& tok : result.value()) {
        if (tok.type == TokenType::K_GENERATE) has_generate = true;
        if (tok.type == TokenType::K_LIMIT) has_limit = true;
    }
    EXPECT_TRUE(has_generate);
    EXPECT_TRUE(has_limit);
}
