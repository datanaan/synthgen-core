#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "common/result.h"

using namespace synthgen;
using namespace synthgen::parser;

class LexerTest : public ::testing::Test {
protected:
    Result<std::vector<Token>> lex(const std::string& src) {
        Lexer l(src);
        return l.tokenize();
    }
};

TEST_F(LexerTest, BasicKeywords) {
    auto result = lex("DEFINE TYPE FLOAT");
    ASSERT_TRUE(result.ok());
    auto& tokens = result.value();
    ASSERT_GE(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(tokens[1].type, TokenType::K_TYPE);
    EXPECT_EQ(tokens[2].type, TokenType::K_FLOAT);
    EXPECT_EQ(tokens[3].type, TokenType::T_EOF);
}

TEST_F(LexerTest, AllV1Keywords) {
    auto result = lex("DEFINE TYPE LOAD DATA INTO FROM CONSTRAINT ON GENERATE TABLE "
                      "WITH LIMIT BETWEEN AND NOT NULL ORDER FLOAT INT DATETIME STRING ENUM");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
    EXPECT_EQ(t[2].type, TokenType::K_LOAD);
    EXPECT_EQ(t[3].type, TokenType::K_DATA);
    EXPECT_EQ(t[4].type, TokenType::K_INTO);
    EXPECT_EQ(t[5].type, TokenType::K_FROM);
    EXPECT_EQ(t[6].type, TokenType::K_CONSTRAINT);
    EXPECT_EQ(t[7].type, TokenType::K_ON);
    EXPECT_EQ(t[8].type, TokenType::K_GENERATE);
    EXPECT_EQ(t[9].type, TokenType::K_TABLE);
    EXPECT_EQ(t[10].type, TokenType::K_WITH);
    EXPECT_EQ(t[11].type, TokenType::K_LIMIT);
    EXPECT_EQ(t[12].type, TokenType::K_BETWEEN);
    EXPECT_EQ(t[13].type, TokenType::K_AND);
    EXPECT_EQ(t[14].type, TokenType::K_NOT);
    EXPECT_EQ(t[15].type, TokenType::K_NULL);
    EXPECT_EQ(t[16].type, TokenType::K_ORDER);
    EXPECT_EQ(t[17].type, TokenType::K_FLOAT);
    EXPECT_EQ(t[18].type, TokenType::K_INT);
    EXPECT_EQ(t[19].type, TokenType::K_DATETIME);
    EXPECT_EQ(t[20].type, TokenType::K_STRING);
    EXPECT_EQ(t[21].type, TokenType::K_ENUM);
}

TEST_F(LexerTest, V2KeywordsRecognized) {
    auto result = lex("DURING WHEN THEN AVG OVER INTERVAL ROWS PARTITION BY SESSION");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::K_DURING);
    EXPECT_EQ(t[1].type, TokenType::K_WHEN);
    EXPECT_EQ(t[2].type, TokenType::K_THEN);
    EXPECT_EQ(t[3].type, TokenType::K_AVG);
    EXPECT_EQ(t[4].type, TokenType::K_OVER);
}

TEST_F(LexerTest, Identifiers) {
    auto result = lex("sensor_log temperature pressure");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::L_IDENT);
    EXPECT_EQ(t[0].lexeme, "sensor_log");
    EXPECT_EQ(t[1].type, TokenType::L_IDENT);
    EXPECT_EQ(t[1].lexeme, "temperature");
    EXPECT_EQ(t[2].type, TokenType::L_IDENT);
    EXPECT_EQ(t[2].lexeme, "pressure");
}

TEST_F(LexerTest, FloatLiterals) {
    auto result = lex("3.14 50.0 1e10 2.5e-3");
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::L_FLOAT);
    EXPECT_EQ(t[0].lexeme, "3.14");
    EXPECT_EQ(t[1].type, TokenType::L_FLOAT);
    EXPECT_EQ(t[1].lexeme, "50.0");
    EXPECT_EQ(t[2].type, TokenType::L_FLOAT);
    EXPECT_EQ(t[3].type, TokenType::L_FLOAT);
}

TEST_F(LexerTest, IntLiterals) {
    auto result = lex("42 1000 0");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::L_INT);
    EXPECT_EQ(t[0].lexeme, "42");
    EXPECT_EQ(t[1].type, TokenType::L_INT);
    EXPECT_EQ(t[2].type, TokenType::L_INT);
}

TEST_F(LexerTest, StringLiterals) {
    auto result = lex("'hello' 'path/to/file'");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::L_STRING);
    EXPECT_EQ(t[0].lexeme, "'hello'");
    EXPECT_EQ(t[1].type, TokenType::L_STRING);
    EXPECT_EQ(t[1].lexeme, "'path/to/file'");
}

TEST_F(LexerTest, Operators) {
    auto result = lex("> < >= <= =");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::S_GT);
    EXPECT_EQ(t[1].type, TokenType::S_LT);
    EXPECT_EQ(t[2].type, TokenType::S_GE);
    EXPECT_EQ(t[3].type, TokenType::S_LE);
    EXPECT_EQ(t[4].type, TokenType::S_EQ);
}

TEST_F(LexerTest, Symbols) {
    auto result = lex("{ } ( ) [ ] , : ;");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::S_LBRACE);
    EXPECT_EQ(t[1].type, TokenType::S_RBRACE);
    EXPECT_EQ(t[2].type, TokenType::S_LPAREN);
    EXPECT_EQ(t[3].type, TokenType::S_RPAREN);
    EXPECT_EQ(t[4].type, TokenType::S_LBRACKET);
    EXPECT_EQ(t[5].type, TokenType::S_RBRACKET);
    EXPECT_EQ(t[6].type, TokenType::S_COMMA);
    EXPECT_EQ(t[7].type, TokenType::S_COLON);
    EXPECT_EQ(t[8].type, TokenType::S_SEMICOLON);
}

TEST_F(LexerTest, CommentsSkipped) {
    auto result = lex("DEFINE -- this is a comment\nTYPE");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
}

TEST_F(LexerTest, LineTracking) {
    auto result = lex("a\nb\nc");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].line, 1);
    EXPECT_EQ(t[1].line, 2);
    EXPECT_EQ(t[2].line, 3);
}

TEST_F(LexerTest, ColumnTracking) {
    auto result = lex("ab cd");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].column, 1);
    EXPECT_EQ(t[1].column, 4);
}

TEST_F(LexerTest, InvalidCharacter) {
    auto result = lex("@#$");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kSyntaxError);
}

TEST_F(LexerTest, UnterminatedString) {
    auto result = lex("'unterminated");
    EXPECT_FALSE(result.ok());
}

TEST_F(LexerTest, CaseInsensitiveKeywords) {
    auto result = lex("define type float");
    ASSERT_TRUE(result.ok());
    auto& t = result.value();
    EXPECT_EQ(t[0].type, TokenType::K_DEFINE);
    EXPECT_EQ(t[1].type, TokenType::K_TYPE);
    EXPECT_EQ(t[2].type, TokenType::K_FLOAT);
}

TEST_F(LexerTest, EmptyInput) {
    auto result = lex("");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].type, TokenType::T_EOF);
}

TEST_F(LexerTest, OnlyComments) {
    auto result = lex("-- comment only\n-- another comment");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].type, TokenType::T_EOF);
}
