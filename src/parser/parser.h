#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/result.h"
#include "parser/ast.h"
#include "parser/lexer.h"

namespace synthgen::parser {

enum class ParseErrorCode {
    kSyntaxError,
    kUndefinedType,
    kDuplicateColumnName,
    kInvalidRange,
    kUnsupportedInV1,
    kTypeMismatch,
    kInvalidEnum,
    kInvalidColumnName,
    kInvalidArgument,
};

struct ParseError {
    ParseErrorCode code;
    std::string message;
    int line = 0;
    int column = 0;
};

struct ParseResult {
    ast::Program program;
    std::vector<ParseError> errors;
};

class Parser {
public:
    Result<ParseResult> parse(const std::string& source);

private:
    Result<ast::Statement> parse_statement();
    Result<ast::DefineTypeStmt> parse_define_type();
    Result<ast::LoadDataStmt> parse_load_data();
    Result<ast::DefineConstraintStmt> parse_define_constraint();
    Result<ast::GenerateTableStmt> parse_generate_table();
    Result<ast::ColumnDef> parse_column_def();
    Result<ast::ConstraintItem> parse_constraint_item();

    bool check(TokenType type) const;
    bool match(TokenType type);
    Token advance();
    Token expect(TokenType type);
    Token peek() const;
    Token previous() const;
    bool is_at_end() const;
    ParseError error(ParseErrorCode code, const std::string& msg);

    std::vector<std::string> defined_types_;
    std::vector<Token> tokens_;
    size_t current_ = 0;
    std::vector<ParseError> errors_;
};

}  // namespace synthgen::parser
