#pragma once

#include <string>

namespace synthgen::parser {

enum class TokenType {
    // v1 Keywords
    K_DEFINE, K_TYPE, K_LOAD, K_DATA, K_INTO, K_FROM,
    K_CONSTRAINT, K_ON, K_GENERATE, K_TABLE, K_WITH, K_LIMIT,
    K_BETWEEN, K_AND, K_NOT, K_NULL, K_ORDER,
    K_FLOAT, K_INT, K_DATETIME, K_STRING, K_ENUM,

    // v2+ Keywords
    K_DURING, K_WHEN, K_THEN,
    K_AVG, K_OVER, K_INTERVAL,
    K_ROWS, K_PARTITION, K_BY, K_SESSION, K_GAP,
    K_UPDATE, K_MODEL, K_INCORPORATE, K_WHERE,
    K_AS, K_OF, K_VERSION,
    K_INCLUDE, K_MODE, K_FALLBACK, K_SAVE,

    // Literals
    L_FLOAT, L_INT, L_STRING, L_IDENT,

    // Symbols
    S_LBRACE, S_RBRACE, S_LPAREN, S_RPAREN,
    S_LBRACKET, S_RBRACKET,
    S_COMMA, S_COLON, S_SEMICOLON,
    S_DOT, S_EQ, S_GT, S_LT, S_GE, S_LE, S_MINUS,

    // Special
    T_EOF, T_ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
};

}  // namespace synthgen::parser
