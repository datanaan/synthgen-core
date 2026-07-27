#include "engine/physics/range_extractor.h"
#include "schema/schema.h"
#include <unordered_map>
#include <algorithm>

namespace synthgen::engine::physics {

RangeExtractor::RangeExtractor(const schema::Schema& schema) : schema_(schema) {}

Result<std::vector<ColumnRange>> RangeExtractor::extract(
    const std::vector<parser::ast::ConstraintItem>& constraints) const {

    struct ColConstraint {
        bool has_min = false, has_max = false;
        double min_val = 0, max_val = 0;
    };
    std::unordered_map<std::string, ColConstraint> ccs;

    for (const auto& c : constraints) {
        auto col_def = schema_.find_column(c.column_name);
        if (!col_def.has_value())
            return Error(ErrorCode::kUndefinedColumn, "Undefined column: " + c.column_name);

        auto& cc = ccs[c.column_name];
        switch (c.op) {
            case parser::ast::ConstraintOperator::kBetween:
                if (!cc.has_min || c.value_min > cc.min_val) {
                    cc.has_min = true; cc.min_val = c.value_min;
                }
                if (!cc.has_max || c.value_max < cc.max_val) {
                    cc.has_max = true; cc.max_val = c.value_max;
                }
                break;
            case parser::ast::ConstraintOperator::kGreaterThan:
                if (!cc.has_min || c.value_min > cc.min_val) {
                    cc.has_min = true; cc.min_val = c.value_min;
                }
                break;
            case parser::ast::ConstraintOperator::kGreaterEqual:
                if (!cc.has_min || c.value_min > cc.min_val) {
                    cc.has_min = true; cc.min_val = c.value_min;
                }
                break;
            case parser::ast::ConstraintOperator::kLessThan:
                if (!cc.has_max || c.value_max < cc.max_val) {
                    cc.has_max = true; cc.max_val = c.value_max;
                }
                break;
            case parser::ast::ConstraintOperator::kLessEqual:
                if (!cc.has_max || c.value_max < cc.max_val) {
                    cc.has_max = true; cc.max_val = c.value_max;
                }
                break;
        }
    }

    std::vector<ColumnRange> ranges;
    for (const auto& col : schema_.columns) {
        ColumnRange range;
        range.column_name = col.name;
        range.type = col.type;
        range.enum_values = col.enum_values;

        if (col.type == DataType::kFloat || col.type == DataType::kInt) {
            range.min_value = col.range_min.value_or(-1e18);
            range.max_value = col.range_max.value_or(1e18);
            auto it = ccs.find(col.name);
            if (it != ccs.end()) {
                if (it->second.has_min) range.min_value = std::max(range.min_value, it->second.min_val);
                if (it->second.has_max) range.max_value = std::min(range.max_value, it->second.max_val);
            }
            if (range.min_value >= range.max_value)
                return Error(ErrorCode::kInvalidRange, "Empty range for: " + col.name);
        } else if (col.type == DataType::kDatetime) {
            range.min_value = 0;
            range.max_value = 31536000000000.0;
        }
        ranges.push_back(std::move(range));
    }
    return ranges;
}

}  // namespace synthgen::engine::physics
