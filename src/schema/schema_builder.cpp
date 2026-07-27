#include "schema/schema_builder.h"

namespace synthgen::schema {

Result<Schema> SchemaBuilder::build(const parser::ast::DefineTypeStmt& stmt) {
    Schema schema;
    schema.type_name = stmt.type_name;

    for (const auto& ast_col : stmt.columns) {
        ColumnDef col;
        col.name = ast_col.name;
        col.type = ast_col.type;
        col.not_null = ast_col.not_null;
        col.is_order = ast_col.is_order;
        col.range_min = ast_col.range_min;
        col.range_max = ast_col.range_max;
        col.enum_values = ast_col.enum_values;
        schema.columns.push_back(std::move(col));
    }

    auto validation = schema.validate();
    if (!validation.ok()) return validation.error();

    return schema;
}

}  // namespace synthgen::schema
