#pragma once
#include "common/result.h"
#include "common/types.h"
#include "scaffold/explain.h"
#include "parser/ast.h"
#include <arrow/table.h>
#include <vector>
#include <string>
#include <optional>

namespace synthgen::schema { struct Schema; }

namespace synthgen::engine::constraint {

struct ValidationFailure {
    int64_t row_index;
    std::string column_name;
    double actual_value;
    double expected_min;
    double expected_max;
    std::string constraint_name;
};

struct ValidationResult {
    int64_t rows_checked = 0;
    int64_t rows_passed = 0;
    int64_t rows_failed = 0;
    double pass_rate = 1.0;
    std::vector<ValidationFailure> failures;  // max 100
};

struct ColumnValidationRule {
    std::string column_name;
    DataType type;
    std::optional<double> min_value;
    std::optional<double> max_value;
    bool min_strict = false;  // true means > (not >=)
    bool max_strict = false;  // true means < (not <=)
    std::string constraint_name;
};

class ValueRangeValidator {
public:
    ValueRangeValidator(const schema::Schema& schema,
                        const std::vector<parser::ast::ConstraintItem>& constraints);

    Result<ValidationResult> validate_batch(std::shared_ptr<arrow::Table> batch);
    scaffold::ExplainInfo explain() const;

private:
    std::vector<ColumnValidationRule> rules_;
};

}  // namespace synthgen::engine::constraint
