#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace synthgen {

enum class DataType { kFloat, kInt, kDatetime, kString, kEnum };

using Timestamp = int64_t;  // microseconds since epoch

struct ColumnDef {
    std::string name;
    DataType type;
    bool not_null = false;
    bool is_order = false;           // v1 reserved, v2 uses for ORDER
    std::optional<double> range_min; // value range declaration
    std::optional<double> range_max;
    std::vector<std::string> enum_values;  // ENUM type values
};

}  // namespace synthgen
