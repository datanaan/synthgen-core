#include "engine/constraint/aggregate_engine.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/array.h>
#include <arrow/chunked_array.h>
#include <cmath>
#include <algorithm>
#include <limits>

namespace synthgen::engine::constraint {

AggregateEngine::AggregateEngine(
    const schema::Schema& schema,
    const std::vector<AggregateConstraintDef>& constraints)
    : schema_(schema), constraints_(constraints) {

    for (int i = 0; i < static_cast<int>(schema_.columns.size()); ++i) {
        if (schema_.columns[i].is_order) {
            order_column_ = schema_.columns[i].name;
            order_column_idx_ = i;
            break;
        }
    }
}

Result<std::vector<AggregationWindow>> AggregateEngine::compute_windows(
    std::shared_ptr<arrow::Table> batch, int64_t interval_us) {

    std::vector<AggregationWindow> windows;
    if (!batch || batch->num_rows() == 0) return windows;

    if (order_column_idx_ < 0) return windows;

    // Get timestamp column values
    auto chunked = batch->column(order_column_idx_);
    std::vector<int64_t> timestamps;
    timestamps.reserve(batch->num_rows());
    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
        auto arr = std::static_pointer_cast<arrow::Int64Array>(chunked->chunk(ci));
        if (arr) {
            for (int64_t j = 0; j < arr->length(); ++j) {
                timestamps.push_back(arr->Value(j));
            }
        }
    }

    if (timestamps.empty()) return windows;

    int64_t window_start = timestamps[0];
    int64_t window_end = window_start + interval_us;
    AggregationWindow current;
    current.window_start = window_start;
    current.window_end = window_end;
    current.start_row = 0;

    for (int64_t i = 0; i < static_cast<int64_t>(timestamps.size()); ++i) {
        if (timestamps[i] >= window_end) {
            // Finalize current window
            current.end_row = i - 1;
            current.is_partial = false;
            for (int64_t r = current.start_row; r <= current.end_row; ++r) {
                current.included_rows.push_back(r);
            }
            windows.push_back(current);

            // Start new window
            window_start = timestamps[i];
            window_end = window_start + interval_us;
            current = AggregationWindow();
            current.window_start = window_start;
            current.window_end = window_end;
            current.start_row = i;
        }
    }

    // Finalize last window (partial)
    current.end_row = static_cast<int64_t>(timestamps.size()) - 1;
    current.is_partial = true;
    for (int64_t r = current.start_row; r <= current.end_row; ++r) {
        current.included_rows.push_back(r);
    }
    windows.push_back(current);

    return windows;
}

Result<double> AggregateEngine::compute_aggregate(
    std::shared_ptr<arrow::Table> batch,
    const AggregationWindow& window,
    const AggregateConstraintDef& constraint) {

    if (window.included_rows.empty()) {
        return Error(ErrorCode::kInvalidState,
                     "Empty window for aggregate", "aggregate_engine");
    }

    // Get column data
    int col_idx = schema_.column_index(constraint.column_name);
    if (col_idx < 0) {
        return Error(ErrorCode::kUndefinedColumn,
                     "Column not found: " + constraint.column_name,
                     "aggregate_engine");
    }

    auto chunked = batch->column(col_idx);
    std::vector<double> values;
    auto type_id = chunked->type()->id();
    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
        auto arr = chunked->chunk(ci);
        if (type_id == arrow::Type::DOUBLE) {
            auto double_arr = std::static_pointer_cast<arrow::DoubleArray>(arr);
            for (int64_t j = 0; j < double_arr->length(); ++j) {
                values.push_back(double_arr->Value(j));
            }
        } else if (type_id == arrow::Type::INT64) {
            auto int_arr = std::static_pointer_cast<arrow::Int64Array>(arr);
            for (int64_t j = 0; j < int_arr->length(); ++j) {
                values.push_back(static_cast<double>(int_arr->Value(j)));
            }
        } else if (type_id == arrow::Type::FLOAT) {
            auto float_arr = std::static_pointer_cast<arrow::FloatArray>(arr);
            for (int64_t j = 0; j < float_arr->length(); ++j) {
                values.push_back(static_cast<double>(float_arr->Value(j)));
            }
        }
    }

    // Collect window values
    std::vector<double> window_values;
    for (auto idx : window.included_rows) {
        if (idx >= 0 && idx < static_cast<int64_t>(values.size())) {
            window_values.push_back(values[idx]);
        }
    }

    if (window_values.empty()) return 0.0;

    double result = 0.0;
    switch (constraint.function) {
        case AggregateFunction::kAvg: {
            // Kahan compensated summation for precision
            double sum = 0.0;
            double compensation = 0.0;
            for (auto v : window_values) {
                double y = v - compensation;
                double t = sum + y;
                compensation = (t - sum) - y;
                sum = t;
            }
            result = sum / static_cast<double>(window_values.size());
            break;
        }
        case AggregateFunction::kSum: {
            // Kahan compensated summation for precision
            double sum = 0.0;
            double compensation = 0.0;
            for (auto v : window_values) {
                double y = v - compensation;
                double t = sum + y;
                compensation = (t - sum) - y;
                sum = t;
            }
            result = sum;
            break;
        }
        case AggregateFunction::kMin: {
            result = *std::min_element(window_values.begin(), window_values.end());
            break;
        }
        case AggregateFunction::kMax: {
            result = *std::max_element(window_values.begin(), window_values.end());
            break;
        }
        case AggregateFunction::kCount: {
            result = static_cast<double>(window_values.size());
            break;
        }
    }
    return result;
}

Result<TwoPhaseResult> AggregateEngine::execute(
    std::shared_ptr<arrow::Table> batch,
    const std::vector<InterRowState>& inter_row_states) {

    scaffold::SpanGuard span("aggregate_engine", "execute", "agg_exec");

    TwoPhaseResult result;

    // Phase one: run inter-row constraints (if any defined via the engine)
    // For now, phase one output = input batch
    result.phase_one_output = batch;

    // Phase two: aggregate constraint validation
    auto p2 = execute_phase_two(batch);
    if (!p2.ok()) return p2.error();

    result.phase_two = p2.value();

    // Compute total exclusion rate
    int64_t total_rows = batch ? batch->num_rows() : 0;
    int64_t excluded_rows = 0;
    for (const auto& wer : result.phase_two.window_exclusion_rates) {
        excluded_rows += static_cast<int64_t>(
            wer.exclusion_rate * static_cast<double>(total_rows) /
            static_cast<double>(std::max(result.phase_two.total_windows, int64_t{1})));
    }
    result.total_exclusion_rate = total_rows > 0
        ? static_cast<double>(excluded_rows) / static_cast<double>(total_rows) : 0.0;

    scaffold::MetricsRegistry::instance().counter("aggregate_execute_total").increment();

    return result;
}

Result<PhaseTwoResult> AggregateEngine::execute_phase_two(
    std::shared_ptr<arrow::Table> phase_one_output) {

    scaffold::SpanGuard span("aggregate_engine", "phase_two", "agg_p2");

    PhaseTwoResult result;

    if (!phase_one_output || phase_one_output->num_rows() == 0) {
        return result;
    }

    for (const auto& constraint : constraints_) {
        int64_t interval_us = constraint.window_interval_us;
        if (interval_us <= 0) continue;

        auto windows_result = compute_windows(phase_one_output, interval_us);
        if (!windows_result.ok()) return windows_result.error();

        auto& windows = windows_result.value();
        result.total_windows += static_cast<int64_t>(windows.size());

        for (auto& window : windows) {
            auto agg_result = compute_aggregate(
                phase_one_output, window, constraint);
            if (!agg_result.ok()) continue;

            double agg_val = agg_result.value();
            bool violated = false;

            if (constraint.min_val.has_value() && agg_val < constraint.min_val.value()) {
                violated = true;
            }
            if (constraint.max_val.has_value() && agg_val > constraint.max_val.value()) {
                violated = true;
            }

            if (violated) {
                result.windows_violated++;
                WindowExclusionRate wer;
                wer.constraint_name = constraint.constraint_name;
                wer.exclusion_rate = 1.0;  // This window is fully excluded
                wer.is_partial = window.is_partial;
                result.window_exclusion_rates.push_back(wer);
            }

            result.windows.push_back(std::move(window));
        }
    }

    scaffold::MetricsRegistry::instance().counter("aggregate_windows_total")
        .increment(result.total_windows);

    return result;
}

scaffold::ExplainInfo AggregateEngine::explain() const {
    scaffold::ExplainInfo info;
    return info;
}

}  // namespace synthgen::engine::constraint
