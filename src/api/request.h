#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace synthgen::api {

struct DefineTypeRequest {
    std::string type_name;
    struct ColumnDef {
        std::string name;
        std::string type;       // "FLOAT", "INT", "DATETIME", "STRING", "ENUM"
        bool not_null = false;
        bool is_order = false;
        std::optional<double> range_min;
        std::optional<double> range_max;
        std::vector<std::string> enum_values;
    };
    std::vector<ColumnDef> columns;
};

struct LoadDataRequest {
    std::string type_name;
    std::string path;
    std::string mode = "strict";  // "strict" or "lenient"
};

struct DefineConstraintRequest {
    std::string constraint_name;
    std::string type_name;
    struct RangeCheck {
        std::string column;
        std::optional<double> min_val;
        std::optional<double> max_val;
    };
    std::vector<RangeCheck> checks;
};

struct ExplainRequest {
    std::string type_name;
    std::vector<std::string> constraints;
};

struct GenerateRequest {
    std::string type_name;
    std::vector<std::string> constraints;
    int64_t limit = 1000;
    std::optional<uint64_t> seed;
    std::string distribution = "uniform";
};

}  // namespace synthgen::api
