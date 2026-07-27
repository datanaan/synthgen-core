#pragma once

#include "common/result.h"
#include "parser/ast.h"
#include "schema/schema.h"

namespace synthgen::schema {

class SchemaBuilder {
public:
    Result<Schema> build(const parser::ast::DefineTypeStmt& stmt);
};

}  // namespace synthgen::schema
