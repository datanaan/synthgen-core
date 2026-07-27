#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "common/types.h"

namespace synthgen::parser::ast {

struct ColumnDef {
    std::string name;
    synthgen::DataType type;
    bool not_null = false;
    bool is_order = false;
    std::optional<double> range_min;
    std::optional<double> range_max;
    std::vector<std::string> enum_values;
};

struct DefineTypeStmt {
    std::string type_name;
    std::vector<ColumnDef> columns;
    std::optional<const ColumnDef*> find_column(const std::string& name) const;
};

struct LoadDataStmt {
    std::string type_name;
    std::string file_path;
};

enum class ConstraintOperator {
    kBetween, kGreaterThan, kLessThan, kGreaterEqual, kLessEqual
};

struct ConstraintItem {
    std::string column_name;
    ConstraintOperator op;
    double value_min = 0;
    double value_max = 0;
};

struct DefineConstraintStmt {
    std::string constraint_name;
    std::string type_name;
    std::vector<ConstraintItem> items;
    std::vector<const ConstraintItem*> get_column_constraints(const std::string& col) const;
};

struct GenerateTableStmt {
    std::string table_name;
    std::string type_name;
    std::string constraint_name;
    int64_t limit = 0;
};

using Statement = std::variant<DefineTypeStmt, LoadDataStmt,
                                DefineConstraintStmt, GenerateTableStmt>;

struct Program {
    std::vector<Statement> statements;
};

}  // namespace synthgen::parser::ast
