#pragma once

#include "common/result.h"
#include "common/types.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "scaffold/explain.h"
#include "schema/schema.h"

#include <arrow/table.h>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace synthgen::engine::constraint {

enum class AggregateFunction {
    kAvg,
    kSum,
    kMin,
    kMax,
    kCount,
};

enum class WindowType {
    kInterval,
};

struct AggregateConstraintDef {
    std::string constraint_name;
    std::string column_name;
    AggregateFunction function;
    WindowType window_type = WindowType::kInterval;
    int64_t window_interval_us = 0;  // Window size in microseconds
    std::optional<double> min_val;
    std::optional<double> max_val;
};

struct AggregationWindow {
    int64_t start_row = 0;
    int64_t end_row = 0;
    Timestamp window_start = 0;
    Timestamp window_end = 0;
    std::vector<int64_t> included_rows;
    bool is_partial = false;
};

struct WindowExclusionRate {
    std::string constraint_name;
    double exclusion_rate = 0.0;
    bool is_partial = false;
};

struct PhaseTwoResult {
    std::vector<AggregationWindow> windows;
    std::vector<WindowExclusionRate> window_exclusion_rates;
    int64_t windows_violated = 0;
    int64_t total_windows = 0;
};

struct TwoPhaseResult {
    std::shared_ptr<arrow::Table> phase_one_output;
    InterRowResult inter_row_result;
    PhaseTwoResult phase_two;
    double total_exclusion_rate = 0.0;
};

class AggregateEngine {
public:
    explicit AggregateEngine(
        const schema::Schema& schema,
        const std::vector<AggregateConstraintDef>& constraints);

    Result<TwoPhaseResult> execute(
        std::shared_ptr<arrow::Table> batch,
        const std::vector<InterRowState>& inter_row_states);

    Result<PhaseTwoResult> execute_phase_two(
        std::shared_ptr<arrow::Table> phase_one_output);

    Result<std::vector<AggregationWindow>> compute_windows(
        std::shared_ptr<arrow::Table> batch,
        int64_t interval_us);

    Result<double> compute_aggregate(
        std::shared_ptr<arrow::Table> batch,
        const AggregationWindow& window,
        const AggregateConstraintDef& constraint);

    scaffold::ExplainInfo explain() const;

private:
    schema::Schema schema_;
    std::vector<AggregateConstraintDef> constraints_;
    std::string order_column_;
    int order_column_idx_ = -1;
};

}  // namespace synthgen::engine::constraint
