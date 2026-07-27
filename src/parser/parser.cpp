#include "parser/parser.h"
#include "parser/ast.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace synthgen::parser {

// --- AST helper implementations ---

std::optional<const ast::ColumnDef*> ast::DefineTypeStmt::find_column(const std::string& name) const {
    for (auto& col : columns) {
        if (col.name == name) return &col;
    }
    return std::nullopt;
}

std::vector<const ast::ConstraintItem*> ast::DefineConstraintStmt::get_column_constraints(
    const std::string& col) const {
    std::vector<const ConstraintItem*> result;
    for (auto& item : items) {
        if (item.column_name == col) result.push_back(&item);
    }
    return result;
}

// --- Parser ---

Result<ParseResult> Parser::parse(const std::string& source) {
    Lexer lexer(source);
    auto lex_result = lexer.tokenize();
    if (!lex_result.ok()) {
        ParseResult result;
        result.errors.push_back({ParseErrorCode::kSyntaxError, lex_result.error().message, 0, 0});
        return result;
    }

    tokens_ = std::move(lex_result).value();
    current_ = 0;
    errors_.clear();
    defined_types_.clear();
    ParseResult result;

    while (!is_at_end()) {
        auto stmt_result = parse_statement();
        if (stmt_result.ok()) {
            result.program.statements.push_back(std::move(stmt_result).value());
        } else {
            // Map ErrorCode to ParseErrorCode for reporting
            ParseErrorCode pec = ParseErrorCode::kSyntaxError;
            if (stmt_result.error().code == ErrorCode::kUnsupportedInV1)
                pec = ParseErrorCode::kUnsupportedInV1;
            else if (stmt_result.error().code == ErrorCode::kUndefinedType)
                pec = ParseErrorCode::kUndefinedType;
            else if (stmt_result.error().code == ErrorCode::kDuplicateColumnName)
                pec = ParseErrorCode::kDuplicateColumnName;
            errors_.push_back({pec, stmt_result.error().message, 0, 0});
            // Skip to next statement
            while (!is_at_end() && peek().type != TokenType::K_DEFINE &&
                   peek().type != TokenType::K_LOAD &&
                   peek().type != TokenType::K_GENERATE) {
                advance();
            }
        }
    }

    result.errors = std::move(errors_);
    return result;
}

Result<ast::Statement> Parser::parse_statement() {
    if (check(TokenType::K_DEFINE)) {
        advance();
        if (check(TokenType::K_TYPE)) {
            auto stmt = parse_define_type();
            if (!stmt.ok()) return stmt.error();
            return ast::Statement(std::move(stmt).value());
        }
        if (check(TokenType::K_CONSTRAINT)) {
            auto stmt = parse_define_constraint();
            if (!stmt.ok()) return stmt.error();
            return ast::Statement(std::move(stmt).value());
        }
        return Error(ErrorCode::kSyntaxError, "Expected TYPE or CONSTRAINT after DEFINE");
    }
    if (check(TokenType::K_LOAD)) {
        auto stmt = parse_load_data();
        if (!stmt.ok()) return stmt.error();
        return ast::Statement(std::move(stmt).value());
    }
    if (check(TokenType::K_GENERATE)) {
        auto stmt = parse_generate_table();
        if (!stmt.ok()) return stmt.error();
        return ast::Statement(std::move(stmt).value());
    }
    return Error(ErrorCode::kSyntaxError,
                 "Expected DEFINE, LOAD, or GENERATE, got '" + peek().lexeme + "'");
}

Result<ast::DefineTypeStmt> Parser::parse_define_type() {
    expect(TokenType::K_TYPE);
    if (is_at_end()) return Error(ErrorCode::kSyntaxError, "Expected type name");
    auto name_token = expect(TokenType::L_IDENT);
    if (name_token.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected type name identifier");

    ast::DefineTypeStmt stmt;
    stmt.type_name = name_token.lexeme;
    defined_types_.push_back(stmt.type_name);

    auto lbrace = expect(TokenType::S_LBRACE);
    if (lbrace.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected '{' after type name");
    if (is_at_end()) return Error(ErrorCode::kSyntaxError, "Expected column definitions");

    std::set<std::string> seen_columns;
    do {
        auto col_result = parse_column_def();
        if (!col_result.ok()) return col_result.error();
        auto& col = col_result.value();
        if (seen_columns.count(col.name)) {
            return Error(ErrorCode::kDuplicateColumnName,
                         "Duplicate column name: " + col.name);
        }
        seen_columns.insert(col.name);
        stmt.columns.push_back(std::move(col));

        if (!check(TokenType::S_COMMA)) break;
        advance();  // consume comma
    } while (!is_at_end() && !check(TokenType::S_RBRACE));

    auto rbrace = expect(TokenType::S_RBRACE);
    if (rbrace.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected '}' to close type definition");
    if (check(TokenType::S_SEMICOLON)) advance();

    if (stmt.columns.empty()) {
        return Error(ErrorCode::kInvalidSchema, "Type must have at least one column");
    }
    return stmt;
}

Result<ast::ColumnDef> Parser::parse_column_def() {
    ast::ColumnDef col;
    auto name_token = expect(TokenType::L_IDENT);
    if (name_token.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected column name");
    col.name = name_token.lexeme;

    auto colon = expect(TokenType::S_COLON);
    if (colon.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected ':' after column name");

    // Parse type
    if (is_at_end()) return Error(ErrorCode::kSyntaxError, "Expected type after ':'");
    auto type_token = advance();

    switch (type_token.type) {
        case TokenType::K_FLOAT: {
            col.type = synthgen::DataType::kFloat;
            if (check(TokenType::S_LBRACKET)) {
                advance();
                bool neg = check(TokenType::S_MINUS); if (neg) advance();
                auto min_tok = advance();
                if (min_tok.type != TokenType::L_FLOAT && min_tok.type != TokenType::L_INT)
                    return Error(ErrorCode::kSyntaxError, "Expected number for range min");
                col.range_min = std::stod(min_tok.lexeme) * (neg ? -1 : 1);
                expect(TokenType::S_COMMA);
                neg = check(TokenType::S_MINUS); if (neg) advance();
                auto max_tok = advance();
                if (max_tok.type != TokenType::L_FLOAT && max_tok.type != TokenType::L_INT)
                    return Error(ErrorCode::kSyntaxError, "Expected number for range max");
                col.range_max = std::stod(max_tok.lexeme) * (neg ? -1 : 1);
                expect(TokenType::S_RBRACKET);
                if (col.range_min >= col.range_max)
                    return Error(ErrorCode::kInvalidRange, "range_min must be less than range_max");
            }
            break;
        }
        case TokenType::K_INT:
            col.type = synthgen::DataType::kInt;
            if (check(TokenType::S_LBRACKET)) {
                advance();
                bool neg = check(TokenType::S_MINUS); if (neg) advance();
                auto min_tok = advance();
                if (min_tok.type != TokenType::L_INT && min_tok.type != TokenType::L_FLOAT)
                    return Error(ErrorCode::kSyntaxError, "Expected integer for range min");
                col.range_min = std::stod(min_tok.lexeme) * (neg ? -1 : 1);
                expect(TokenType::S_COMMA);
                neg = check(TokenType::S_MINUS); if (neg) advance();
                auto max_tok = advance();
                if (max_tok.type != TokenType::L_INT && max_tok.type != TokenType::L_FLOAT)
                    return Error(ErrorCode::kSyntaxError, "Expected integer for range max");
                col.range_max = std::stod(max_tok.lexeme) * (neg ? -1 : 1);
                expect(TokenType::S_RBRACKET);
                if (col.range_min >= col.range_max)
                    return Error(ErrorCode::kInvalidRange, "range_min must be less than range_max");
            }
            break;

        case TokenType::K_DATETIME:
            col.type = synthgen::DataType::kDatetime;
            break;
        case TokenType::K_STRING:
            col.type = synthgen::DataType::kString;
            break;
        case TokenType::K_ENUM: {
            col.type = synthgen::DataType::kEnum;
            expect(TokenType::S_LPAREN);
            do {
                auto val = expect(TokenType::L_STRING);
                if (val.type == TokenType::T_ERROR)
                    return Error(ErrorCode::kSyntaxError, "Expected string value in ENUM");
                // Strip quotes
                col.enum_values.push_back(val.lexeme.substr(1, val.lexeme.size() - 2));
                if (!check(TokenType::S_COMMA)) break;
                advance();
            } while (!is_at_end() && !check(TokenType::S_RPAREN));
            expect(TokenType::S_RPAREN);
            if (col.enum_values.empty())
                return Error(ErrorCode::kInvalidEnum, "ENUM must have at least one value");
            break;
        }
        default:
            return Error(ErrorCode::kSyntaxError,
                         "Expected type (FLOAT, INT, DATETIME, STRING, ENUM)");
    }

    // Optional modifiers
    while (!is_at_end()) {
        if (check(TokenType::K_NOT)) {
            advance();
            expect(TokenType::K_NULL);
            col.not_null = true;
        } else if (check(TokenType::K_ORDER)) {
            advance();
            col.is_order = true;
        } else {
            break;
        }
    }

    return col;
}

Result<ast::LoadDataStmt> Parser::parse_load_data() {
    expect(TokenType::K_LOAD);
    expect(TokenType::K_DATA);
    expect(TokenType::K_INTO);

    auto type_tok = expect(TokenType::L_IDENT);
    if (type_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected type name after INTO");

    // Check type is defined
    auto it = std::find(defined_types_.begin(), defined_types_.end(), type_tok.lexeme);
    if (it == defined_types_.end()) {
        return Error(ErrorCode::kUndefinedType,
                     "Undefined type: " + type_tok.lexeme);
    }

    expect(TokenType::K_FROM);

    auto path_tok = expect(TokenType::L_STRING);
    if (path_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected file path string after FROM");

    if (check(TokenType::S_SEMICOLON)) advance();

    ast::LoadDataStmt stmt;
    stmt.type_name = type_tok.lexeme;
    stmt.file_path = path_tok.lexeme.substr(1, path_tok.lexeme.size() - 2);
    return stmt;
}

Result<ast::DefineConstraintStmt> Parser::parse_define_constraint() {
    expect(TokenType::K_CONSTRAINT);

    auto name_tok = expect(TokenType::L_IDENT);
    if (name_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected constraint name");

    expect(TokenType::K_ON);

    auto type_tok = expect(TokenType::L_IDENT);
    if (type_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected type name after ON");

    auto it = std::find(defined_types_.begin(), defined_types_.end(), type_tok.lexeme);
    if (it == defined_types_.end()) {
        return Error(ErrorCode::kUndefinedType,
                     "Undefined type: " + type_tok.lexeme);
    }

    expect(TokenType::S_LBRACE);

    ast::DefineConstraintStmt stmt;
    stmt.constraint_name = name_tok.lexeme;
    stmt.type_name = type_tok.lexeme;

    do {
        // Check for v1 unsupported keywords
        if (check(TokenType::K_DURING)) {
            return Error(ErrorCode::kUnsupportedInV1,
                         "DURING constraints are not supported in v1. Supported from v2.");
        }
        if (check(TokenType::K_WHEN)) {
            return Error(ErrorCode::kUnsupportedInV1,
                         "WHEN constraints are not supported in v1. Supported from v2.");
        }
        if (check(TokenType::K_AVG) || check(TokenType::K_OVER)) {
            return Error(ErrorCode::kUnsupportedInV1,
                         "Aggregate constraints are not supported in v1. Supported from v2.");
        }

        auto item_result = parse_constraint_item();
        if (!item_result.ok()) return item_result.error();
        stmt.items.push_back(std::move(item_result).value());

        if (!check(TokenType::S_COMMA)) break;
        advance();
        // After comma, check for v1 unsupported keywords
        if (check(TokenType::K_DURING)) {
            return Error(ErrorCode::kUnsupportedInV1,
                         "DURING constraints are not supported in v1. Supported from v2.");
        }
        if (check(TokenType::K_WHEN)) {
            return Error(ErrorCode::kUnsupportedInV1,
                         "WHEN constraints are not supported in v1. Supported from v2.");
        }
    } while (!is_at_end() && !check(TokenType::S_RBRACE));

    // Check if we stopped because of an unsupported keyword
    if (check(TokenType::K_DURING)) {
        return Error(ErrorCode::kUnsupportedInV1,
                     "DURING constraints are not supported in v1. Supported from v2.");
    }
    if (check(TokenType::K_WHEN)) {
        return Error(ErrorCode::kUnsupportedInV1,
                     "WHEN constraints are not supported in v1. Supported from v2.");
    }
    if (check(TokenType::K_AVG) || check(TokenType::K_OVER)) {
        return Error(ErrorCode::kUnsupportedInV1,
                     "Aggregate constraints are not supported in v1. Supported from v2.");
    }

    auto cbr = expect(TokenType::S_RBRACE);
    if (cbr.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected '}' to close constraint definition");
    if (check(TokenType::S_SEMICOLON)) advance();

    return stmt;
}

Result<ast::ConstraintItem> Parser::parse_constraint_item() {
    ast::ConstraintItem item;
    auto col_tok = expect(TokenType::L_IDENT);
    if (col_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected column name in constraint");
    item.column_name = col_tok.lexeme;

    if (check(TokenType::K_BETWEEN)) {
        advance();
        item.op = ast::ConstraintOperator::kBetween;
        bool neg_min = check(TokenType::S_MINUS); if (neg_min) advance();
        auto min_tok = advance();
        if (min_tok.type != TokenType::L_FLOAT && min_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after BETWEEN");
        item.value_min = std::stod(min_tok.lexeme) * (neg_min ? -1 : 1);
        expect(TokenType::K_AND);
        bool neg_max = check(TokenType::S_MINUS); if (neg_max) advance();
        auto max_tok = advance();
        if (max_tok.type != TokenType::L_FLOAT && max_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after AND");
        item.value_max = std::stod(max_tok.lexeme) * (neg_max ? -1 : 1);
    } else if (check(TokenType::S_GT)) {
        advance();
        item.op = ast::ConstraintOperator::kGreaterThan;
        auto val_tok = advance();
        bool neg = check(TokenType::S_MINUS); if (neg) advance();
        if (val_tok.type != TokenType::L_FLOAT && val_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after >");
        item.value_min = std::stod(val_tok.lexeme) * (neg ? -1 : 1);
    } else if (check(TokenType::S_LT)) {
        advance();
        item.op = ast::ConstraintOperator::kLessThan;
        auto val_tok = advance();
        bool neg = check(TokenType::S_MINUS); if (neg) advance();
        if (val_tok.type != TokenType::L_FLOAT && val_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after <");
        item.value_max = std::stod(val_tok.lexeme) * (neg ? -1 : 1);
    } else if (check(TokenType::S_GE)) {
        advance();
        item.op = ast::ConstraintOperator::kGreaterEqual;
        auto val_tok = advance();
        bool neg = check(TokenType::S_MINUS); if (neg) advance();
        if (val_tok.type != TokenType::L_FLOAT && val_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after >=");
        item.value_min = std::stod(val_tok.lexeme) * (neg ? -1 : 1);
    } else if (check(TokenType::S_LE)) {
        advance();
        item.op = ast::ConstraintOperator::kLessEqual;
        auto val_tok = advance();
        bool neg = check(TokenType::S_MINUS); if (neg) advance();
        if (val_tok.type != TokenType::L_FLOAT && val_tok.type != TokenType::L_INT)
            return Error(ErrorCode::kSyntaxError, "Expected number after <=");
        item.value_max = std::stod(val_tok.lexeme) * (neg ? -1 : 1);
    } else {
        return Error(ErrorCode::kSyntaxError,
                     "Expected constraint operator (BETWEEN, >, <, >=, <=)");
    }

    return item;
}

Result<ast::GenerateTableStmt> Parser::parse_generate_table() {
    expect(TokenType::K_GENERATE);
    expect(TokenType::K_TABLE);

    auto name_tok = expect(TokenType::L_IDENT);
    if (name_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected table name");

    expect(TokenType::K_FROM);

    auto type_tok = expect(TokenType::L_IDENT);
    if (type_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected type name after FROM");

    auto it = std::find(defined_types_.begin(), defined_types_.end(), type_tok.lexeme);
    if (it == defined_types_.end()) {
        return Error(ErrorCode::kUndefinedType, "Undefined type: " + type_tok.lexeme);
    }

    expect(TokenType::K_WITH);
    // CONSTRAINTS is not a keyword, just consume identifier
    if (!check(TokenType::K_CONSTRAINT) && !check(TokenType::L_IDENT)) {
        return Error(ErrorCode::kSyntaxError, "Expected CONSTRAINTS after WITH");
    }
    advance();  // consume CONSTRAINTS

    auto constraint_tok = expect(TokenType::L_IDENT);
    if (constraint_tok.type == TokenType::T_ERROR)
        return Error(ErrorCode::kSyntaxError, "Expected constraint name");

    expect(TokenType::K_LIMIT);

    auto limit_tok = advance();
    if (limit_tok.type != TokenType::L_INT)
        return Error(ErrorCode::kSyntaxError, "Expected integer after LIMIT");

    int64_t limit = std::stoll(limit_tok.lexeme);
    if (limit < 0)
        return Error(ErrorCode::kInvalidArgument, "LIMIT must be non-negative");

    if (check(TokenType::S_SEMICOLON)) advance();

    ast::GenerateTableStmt stmt;
    stmt.table_name = name_tok.lexeme;
    stmt.type_name = type_tok.lexeme;
    stmt.constraint_name = constraint_tok.lexeme;
    stmt.limit = limit;
    return stmt;
}

// --- Token helpers ---

bool Parser::check(TokenType type) const {
    return !is_at_end() && peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}

Token Parser::advance() {
    if (!is_at_end()) current_++;
    return previous();
}

Token Parser::expect(TokenType type) {
    if (check(type)) return advance();
    return Token{TokenType::T_ERROR,
                 "Expected " + std::to_string(static_cast<int>(type)) +
                 " but got '" + peek().lexeme + "'",
                 peek().line, peek().column};
}

Token Parser::peek() const {
    return tokens_[current_];
}

Token Parser::previous() const {
    return tokens_[current_ - 1];
}

bool Parser::is_at_end() const {
    return current_ >= tokens_.size() || tokens_[current_].type == TokenType::T_EOF;
}

ParseError Parser::error(ParseErrorCode code, const std::string& msg) {
    return ParseError{code, msg, peek().line, peek().column};
}

}  // namespace synthgen::parser
