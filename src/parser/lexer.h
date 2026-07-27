#pragma once

#include "parser/token.h"
#include "common/result.h"

#include <string>
#include <string_view>
#include <vector>

namespace synthgen::parser {

struct LexError {
    std::string message;
    int line;
    int column;
};

class Lexer {
public:
    explicit Lexer(std::string_view source);
    Result<std::vector<Token>> tokenize();

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool is_at_end() const;
    bool match(char expected);
    void skip_whitespace_and_comments();
    Token scan_token();
    Token scan_string();
    Token scan_number();
    Token scan_identifier_or_keyword();
    Token make_token(TokenType type) const;
    Token error_token(const std::string& msg) const;

    std::string source_;
    size_t start_ = 0;
    size_t current_ = 0;
    int line_ = 1;
    int column_ = 1;
    int token_column_ = 1;
};

}  // namespace synthgen::parser
