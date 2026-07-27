#include "schema/schema.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace synthgen::schema {

Result<void> Schema::validate() const {
    if (type_name.empty()) {
        return Error(ErrorCode::kInvalidSchema, "Schema type_name cannot be empty");
    }
    if (columns.empty()) {
        return Error(ErrorCode::kInvalidSchema, "Schema must have at least one column");
    }

    std::set<std::string> seen;
    for (const auto& col : columns) {
        if (col.name.empty()) {
            return Error(ErrorCode::kInvalidColumnName, "Column name cannot be empty");
        }
        if (seen.count(col.name)) {
            return Error(ErrorCode::kDuplicateColumnName,
                         "Duplicate column name: " + col.name);
        }
        seen.insert(col.name);

        if (col.range_min.has_value() && col.range_max.has_value()) {
            if (std::isnan(col.range_min.value()) || std::isnan(col.range_max.value())) {
                return Error(ErrorCode::kInvalidRange,
                             "range_min/range_max must not be NaN for column: " + col.name);
            }
            if (col.range_min.value() >= col.range_max.value()) {
                return Error(ErrorCode::kInvalidRange,
                             "range_min must be less than range_max for column: " + col.name);
            }
        }

        if (col.type == DataType::kEnum && col.enum_values.empty()) {
            return Error(ErrorCode::kInvalidEnum,
                         "ENUM column must have at least one value: " + col.name);
        }
    }

    // Check ORDER columns exist
    auto order_cols = order_columns();
    for (const auto& oc : order_cols) {
        if (!find_column(oc).has_value()) {
            return Error(ErrorCode::kUndefinedColumn,
                         "ORDER column not found: " + oc);
        }
    }

    return {};
}

std::optional<ColumnDef> Schema::find_column(const std::string& name) const {
    for (const auto& col : columns) {
        if (col.name == name) return col;
    }
    return std::nullopt;
}

int Schema::column_index(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
        if (columns[i].name == name) return i;
    }
    return -1;
}

std::vector<std::string> Schema::order_columns() const {
    std::vector<std::string> result;
    for (const auto& col : columns) {
        if (col.is_order) result.push_back(col.name);
    }
    return result;
}

}  // namespace synthgen::schema
