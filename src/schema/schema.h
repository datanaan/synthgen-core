#pragma once

#include <optional>
#include <string>
#include <vector>

#include "common/result.h"
#include "common/types.h"

namespace synthgen::schema {

struct Schema {
    std::string type_name;
    std::vector<ColumnDef> columns;

    Result<void> validate() const;
    std::optional<ColumnDef> find_column(const std::string& name) const;
    int column_index(const std::string& name) const;
    std::vector<std::string> order_columns() const;
};

}  // namespace synthgen::schema
