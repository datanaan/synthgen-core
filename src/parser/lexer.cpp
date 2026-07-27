#include "parser/lexer.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace synthgen::parser {

namespace {

const std::unordered_map<std::string, TokenType>& keyword_map() {
    static const std::unordered_map<std::string, TokenType> map = {
        {"DEFINE", TokenType::K_DEFINE}, {"TYPE", TokenType::K_TYPE},
        {"LOAD", TokenType::K_LOAD}, {"DATA", TokenType::K_DATA},
        {"INTO", TokenType::K_INTO}, {"FROM", TokenType::K_FROM},
        {"CONSTRAINT", TokenType::K_CONSTRAINT}, {"ON", TokenType::K_ON},
        {"GENERATE", TokenType::K_GENERATE}, {"TABLE", TokenType::K_TABLE},
        {"WITH", TokenType::K_WITH}, {"LIMIT", TokenType::K_LIMIT},
        {"BETWEEN", TokenType::K_BETWEEN}, {"AND", TokenType::K_AND},
        {"NOT", TokenType::K_NOT}, {"NULL", TokenType::K_NULL},
        {"ORDER", TokenType::K_ORDER},
        {"FLOAT", TokenType::K_FLOAT}, {"INT", TokenType::K_INT},
        {"DATETIME", TokenType::K_DATETIME}, {"STRING", TokenType::K_STRING},
        {"ENUM", TokenType::K_ENUM},
        {"DURING", TokenType::K_DURING}, {"WHEN", TokenType::K_WHEN},
        {"THEN", TokenType::K_THEN},
        {"AVG", TokenType::K_AVG}, {"OVER", TokenType::K_OVER},
        {"INTERVAL", TokenType::K_INTERVAL},
        {"ROWS", TokenType::K_ROWS}, {"PARTITION", TokenType::K_PARTITION},
        {"BY", TokenType::K_BY}, {"SESSION", TokenType::K_SESSION},
        {"GAP", TokenType::K_GAP},
        {"UPDATE", TokenType::K_UPDATE}, {"MODEL", TokenType::K_MODEL},
        {"INCORPORATE", TokenType::K_INCORPORATE}, {"WHERE", TokenType::K_WHERE},
        {"AS", TokenType::K_AS}, {"OF", TokenType::K_OF},
        {"VERSION", TokenType::K_VERSION},
        {"INCLUDE", TokenType::K_INCLUDE}, {"MODE", TokenType::K_MODE},
        {"FALLBACK", TokenType::K_FALLBACK}, {"SAVE", TokenType::K_SAVE},
    };
    return map;
}

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

}  // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[current_];
}

char Lexer::peek_next() const {
    if (current_ + 1 >= source_.size()) return '\0';
    return source_[current_ + 1];
}

char Lexer::advance() {
    char c = source_[current_++];
    if (c == '\n') { line_++; column_ = 1; }
    else { column_++; }
    return c;
}

bool Lexer::is_at_end() const { return current_ >= source_.size(); }

bool Lexer::match(char expected) {
    if (is_at_end() || source_[current_] != expected) return false;
    advance();
    return true;
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '-' && peek_next() == '-') {
            while (!is_at_end() && peek() != '\n') advance();
        } else {
            break;
        }
    }
    start_ = current_;
    token_column_ = column_;
}

Token Lexer::make_token(TokenType type) const {
    return Token{type, source_.substr(start_, current_ - start_), line_, token_column_};
}

Token Lexer::error_token(const std::string& msg) const {
    return Token{TokenType::T_ERROR, msg, line_, token_column_};
}

Token Lexer::scan_string() {
    while (!is_at_end() && peek() != '\'') advance();
    if (is_at_end()) return error_token("Unterminated string");
    advance();
    return make_token(TokenType::L_STRING);
}

Token Lexer::scan_number() {
    while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    bool is_float = false;
    if (!is_at_end() && peek() == '.' && peek_next() != '.') {
        is_float = true;
        advance();
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    if (!is_at_end() && (peek() == 'e' || peek() == 'E')) {
        is_float = true;
        advance();
        if (!is_at_end() && (peek() == '+' || peek() == '-')) advance();
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    return make_token(is_float ? TokenType::L_FLOAT : TokenType::L_INT);
}

Token Lexer::scan_identifier_or_keyword() {
    while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
        advance();
    std::string text = source_.substr(start_, current_ - start_);
    auto it = keyword_map().find(to_upper(text));
    if (it != keyword_map().end()) return make_token(it->second);
    return make_token(TokenType::L_IDENT);
}

Token Lexer::scan_token() {
    skip_whitespace_and_comments();
    if (is_at_end()) return make_token(TokenType::T_EOF);
    start_ = current_;
    token_column_ = column_;
    char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        return scan_identifier_or_keyword();
    if (std::isdigit(static_cast<unsigned char>(c)))
        return scan_number();

    switch (c) {
        case '\'': return scan_string();
        case '{': return make_token(TokenType::S_LBRACE);
        case '}': return make_token(TokenType::S_RBRACE);
        case '(': return make_token(TokenType::S_LPAREN);
        case ')': return make_token(TokenType::S_RPAREN);
        case '[': return make_token(TokenType::S_LBRACKET);
        case ']': return make_token(TokenType::S_RBRACKET);
        case ',': return make_token(TokenType::S_COMMA);
        case ':': return make_token(TokenType::S_COLON);
        case ';': return make_token(TokenType::S_SEMICOLON);
        case '.': return make_token(TokenType::S_DOT);
        case '=': return make_token(TokenType::S_EQ);
        case '>': return make_token(match('=') ? TokenType::S_GE : TokenType::S_GT);
        case '<': return make_token(match('=') ? TokenType::S_LE : TokenType::S_LT);
        case '-':
            if (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek())))
                return scan_number();
            return make_token(TokenType::S_MINUS);
        default:
            return error_token(std::string("Unexpected character: '") + c + "'");
    }
}

Result<std::vector<Token>> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = scan_token();
        if (token.type == TokenType::T_ERROR) {
            return Error(ErrorCode::kSyntaxError, token.lexeme, "lexer");
        }
        tokens.push_back(token);
        if (token.type == TokenType::T_EOF) break;
    }
    return tokens;
}

}  // namespace synthgen::parser
