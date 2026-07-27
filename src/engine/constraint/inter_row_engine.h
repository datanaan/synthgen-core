#pragma once

#include "common/result.h"
#include "common/types.h"
#include "scaffold/explain.h"
#include "schema/schema.h"

#include <arrow/table.h>
#include <deque>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace synthgen::engine::constraint {

struct InterRowConstraintDef {
    std::string column_name;
    std::string order_column;

    enum class Type {
        kDeltaMax,         // |x[t] - x[t-1]| < delta_max
        kDeltaMin,         // |x[t] - x[t-1]| > delta_min
        kMonotoneIncrease, // x[t] > x[t-1]
        kMonotoneDecrease, // x[t] < x[t-1]
    };

    Type type;
    std::optional<double> delta_max;
    std::optional<double> delta_min;
};

struct InterRowState {
    std::optional<double> last_value;
    bool initialized = false;
    std::string column_name;
};

struct InterRowResult {
    std::shared_ptr<arrow::Table> filtered_batch;
    std::vector<InterRowState> outgoing_states;
    int64_t rows_passed = 0;
    int64_t rows_filtered = 0;
    double filter_rate = 0.0;
};

class InterRowEngine {
public:
    explicit InterRowEngine(
        const schema::Schema& schema,
        const std::vector<InterRowConstraintDef>& constraints);

    Result<InterRowResult> execute_batch(
        std::shared_ptr<arrow::Table> batch,
        const std::vector<InterRowState>& incoming_states);

    const std::string& order_column() const { return order_column_; }

    scaffold::ExplainInfo explain() const;

private:
    schema::Schema schema_;
    std::vector<InterRowConstraintDef> constraints_;
    std::string order_column_;

    Result<bool> check_constraint(
        const InterRowConstraintDef& constraint,
        double current_value,
        double previous_value) const;

    Result<void> validate_constraints() const;
};

}  // namespace synthgen::engine::constraint
