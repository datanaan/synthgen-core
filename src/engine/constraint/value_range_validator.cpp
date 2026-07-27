#include "engine/constraint/value_range_validator.h"
#include "schema/schema.h"
#include "scaffold/trace.h"
#include <arrow/array.h>
#include <arrow/table.h>

namespace synthgen::engine::constraint {

ValueRangeValidator::ValueRangeValidator(
    const schema::Schema& schema,
    const std::vector<parser::ast::ConstraintItem>& constraints) {

    // Build validation rules from constraints
    for (const auto& c : constraints) {
        auto col = schema.find_column(c.column_name);
        if (!col.has_value()) continue;

        ColumnValidationRule rule;
        rule.column_name = c.column_name;
        rule.type = col->type;
        rule.constraint_name = "constraint";

        switch (c.op) {
            case parser::ast::ConstraintOperator::kBetween:
                rule.min_value = c.value_min;
                rule.max_value = c.value_max;
                break;
            case parser::ast::ConstraintOperator::kGreaterThan:
                rule.min_value = c.value_min;
                rule.min_strict = true;  // > means strictly greater
                break;
            case parser::ast::ConstraintOperator::kGreaterEqual:
                rule.min_value = c.value_min;
                break;
            case parser::ast::ConstraintOperator::kLessThan:
                rule.max_value = c.value_max;
                rule.max_strict = true;  // < means strictly less
                break;
            case parser::ast::ConstraintOperator::kLessEqual:
                rule.max_value = c.value_max;
                break;
        }
        rules_.push_back(std::move(rule));
    }
}

Result<ValidationResult> ValueRangeValidator::validate_batch(
    std::shared_ptr<arrow::Table> batch) {
    scaffold::SpanGuard span("validator", "validate_batch", "val-0");

    if (!batch) {
        return Error(ErrorCode::kInvalidArgument,
                     "batch must not be null", "validator");
    }

    ValidationResult result;
    int64_t num_rows = batch->num_rows();
    result.rows_checked = num_rows;

    if (num_rows == 0) {
        result.pass_rate = 1.0;
        return result;
    }

    for (const auto& rule : rules_) {
        int col_idx = batch->schema()->GetFieldIndex(rule.column_name);
        if (col_idx < 0) {
            return Error(ErrorCode::kUndefinedColumn,
                         "Column not found in batch: " + rule.column_name, "validator");
        }

        auto column = batch->column(col_idx);
        if (column->type()->id() == arrow::Type::DOUBLE) {
            for (int c = 0; c < column->num_chunks(); c++) {
                auto arr = std::static_pointer_cast<arrow::DoubleArray>(column->chunk(c));
                for (int64_t r = 0; r < arr->length(); r++) {
                    if (arr->IsNull(r)) continue;
                    double val = arr->Value(r);
                    bool failed = false;
                    if (rule.min_value.has_value()) {
                        if (rule.min_strict) {
                            if (val <= rule.min_value.value()) failed = true;
                        } else {
                            if (val < rule.min_value.value()) failed = true;
                        }
                    }
                    if (rule.max_value.has_value()) {
                        if (rule.max_strict) {
                            if (val >= rule.max_value.value()) failed = true;
                        } else {
                            if (val > rule.max_value.value()) failed = true;
                        }
                    }

                    if (failed) {
                        result.rows_failed++;
                        if (static_cast<int64_t>(result.failures.size()) < 100) {
                            result.failures.push_back({
                                r, rule.column_name, val,
                                rule.min_value.value_or(0),
                                rule.max_value.value_or(0),
                                rule.constraint_name
                            });
                        }
                    }
                }
            }
        } else if (column->type()->id() == arrow::Type::INT64) {
            for (int c = 0; c < column->num_chunks(); c++) {
                auto arr = std::static_pointer_cast<arrow::Int64Array>(column->chunk(c));
                for (int64_t r = 0; r < arr->length(); r++) {
                    if (arr->IsNull(r)) continue;
                    double val = static_cast<double>(arr->Value(r));
                    bool failed = false;
                    if (rule.min_value.has_value()) {
                        if (rule.min_strict) {
                            if (val <= rule.min_value.value()) failed = true;
                        } else {
                            if (val < rule.min_value.value()) failed = true;
                        }
                    }
                    if (rule.max_value.has_value()) {
                        if (rule.max_strict) {
                            if (val >= rule.max_value.value()) failed = true;
                        } else {
                            if (val > rule.max_value.value()) failed = true;
                        }
                    }

                    if (failed) {
                        result.rows_failed++;
                        if (static_cast<int64_t>(result.failures.size()) < 100) {
                            result.failures.push_back({
                                r, rule.column_name, val,
                                rule.min_value.value_or(0),
                                rule.max_value.value_or(0),
                                rule.constraint_name
                            });
                        }
                    }
                }
            }
        }
    }

    result.rows_passed = num_rows - result.rows_failed;
    result.pass_rate = num_rows > 0 ? static_cast<double>(result.rows_passed) / num_rows : 1.0;
    return result;
}

scaffold::ExplainInfo ValueRangeValidator::explain() const {
    scaffold::ExplainInfo info;
    info.execution_mode = scaffold::ExecutionMode::kRowByRow;
    info.path = "value_range_validation";
    info.constraint_classification.value_range = static_cast<int>(rules_.size());
    return info;
}

}  // namespace synthgen::engine::constraint
