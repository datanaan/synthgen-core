#include "engine/constraint/inter_row_engine.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/array.h>
#include <arrow/table.h>
#include <arrow/chunked_array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <cmath>
#include <algorithm>

namespace synthgen::engine::constraint {

InterRowEngine::InterRowEngine(
    const schema::Schema& schema,
    const std::vector<InterRowConstraintDef>& constraints)
    : schema_(schema), constraints_(constraints) {

    // Find ORDER column from schema
    for (const auto& col : schema_.columns) {
        if (col.is_order) {
            order_column_ = col.name;
            break;
        }
    }
}

Result<void> InterRowEngine::validate_constraints() const {
    if (constraints_.empty()) return {};

    // Must have ORDER column
    if (order_column_.empty()) {
        return Error(ErrorCode::kOrderColumnRequired,
                     "Inter-row constraints require an ORDER column in schema",
                     "inter_row_engine");
    }

    for (const auto& c : constraints_) {
        // Column must exist
        auto col = schema_.find_column(c.column_name);
        if (!col.has_value()) {
            return Error(ErrorCode::kUndefinedColumn,
                         "Column not found: " + c.column_name,
                         "inter_row_engine");
        }
        // Column must be numeric
        if (col->type != DataType::kFloat && col->type != DataType::kInt) {
            return Error(ErrorCode::kTypeMismatch,
                         "Inter-row constraint column must be numeric: " + c.column_name,
                         "inter_row_engine");
        }
        // Delta must be positive
        if (c.type == InterRowConstraintDef::Type::kDeltaMax) {
            if (!c.delta_max.has_value() || c.delta_max.value() <= 0) {
                return Error(ErrorCode::kInvalidDelta,
                             "delta_max must be > 0 for: " + c.column_name,
                             "inter_row_engine");
            }
        }
        if (c.type == InterRowConstraintDef::Type::kDeltaMin) {
            if (!c.delta_min.has_value() || c.delta_min.value() <= 0) {
                return Error(ErrorCode::kInvalidDelta,
                             "delta_min must be > 0 for: " + c.column_name,
                             "inter_row_engine");
            }
        }
    }
    return {};
}

Result<bool> InterRowEngine::check_constraint(
    const InterRowConstraintDef& constraint,
    double current_value,
    double previous_value) const {

    double delta = std::abs(current_value - previous_value);

    switch (constraint.type) {
        case InterRowConstraintDef::Type::kDeltaMax:
            return delta < constraint.delta_max.value();
        case InterRowConstraintDef::Type::kDeltaMin:
            return delta > constraint.delta_min.value();
        case InterRowConstraintDef::Type::kMonotoneIncrease:
            return current_value > previous_value;
        case InterRowConstraintDef::Type::kMonotoneDecrease:
            return current_value < previous_value;
    }
    return false;
}

Result<InterRowResult> InterRowEngine::execute_batch(
    std::shared_ptr<arrow::Table> batch,
    const std::vector<InterRowState>& incoming_states) {

    scaffold::SpanGuard span("inter_row_engine", "execute_batch", "ir_exec");

    // Validate constraints
    auto vr = validate_constraints();
    if (!vr.ok()) return vr.error();

    // Handle empty batch
    if (!batch || batch->num_rows() == 0) {
        InterRowResult result;
        result.filtered_batch = batch;
        for (const auto& c : constraints_) {
            InterRowState s;
            s.column_name = c.column_name;
            result.outgoing_states.push_back(s);
        }
        scaffold::MetricsRegistry::instance().counter("inter_row_rows_checked").increment(0);
        return result;
    }

    // Get column indices and flatten chunked arrays
    struct ColInfo {
        int idx;
        std::vector<double> values;  // flattened column data
    };
    std::vector<ColInfo> col_infos;
    for (const auto& c : constraints_) {
        int idx = schema_.column_index(c.column_name);
        if (idx < 0) {
            return Error(ErrorCode::kUndefinedColumn,
                         "Column index not found: " + c.column_name,
                         "inter_row_engine");
        }
        ColInfo info;
        info.idx = idx;
        // Flatten chunked array to doubles
        auto chunked = batch->column(idx);
        for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
            auto arr = chunked->chunk(ci);
            auto double_arr = std::static_pointer_cast<arrow::DoubleArray>(arr);
            if (double_arr) {
                for (int64_t j = 0; j < double_arr->length(); ++j) {
                    info.values.push_back(double_arr->Value(j));
                }
            }
        }
        col_infos.push_back(std::move(info));
    }

    int64_t rows = batch->num_rows();
    int64_t rows_passed = 0;
    int64_t rows_filtered = 0;

    // Build incoming state map: column_name -> last_value
    std::map<std::string, double> prev_values;
    for (const auto& state : incoming_states) {
        if (state.initialized && state.last_value.has_value()) {
            prev_values[state.column_name] = *state.last_value;
        }
    }

    // Track which rows pass all constraints
    std::vector<bool> pass(rows, true);

    // Track last passing index per constraint column for O(n) lookup
    std::vector<int64_t> last_pass_idx(constraints_.size(), -1);

    // For each row, check all constraints
    for (int64_t i = 0; i < rows; ++i) {
        bool row_passes = true;

        for (size_t ci = 0; ci < constraints_.size(); ++ci) {
            const auto& constraint = constraints_[ci];
            const auto& info = col_infos[ci];

            double current = info.values[i];

            // Get previous value
            double previous;
            if (last_pass_idx[ci] < 0) {
                auto it = prev_values.find(constraint.column_name);
                if (it == prev_values.end()) {
                    continue;  // No incoming state, first row passes
                }
                previous = it->second;
            } else {
                previous = info.values[last_pass_idx[ci]];
            }

            auto check = check_constraint(constraint, current, previous);
            if (!check.ok()) {
                row_passes = false;
                break;
            }
            if (!check.value()) {
                row_passes = false;
                break;
            }
        }

        pass[i] = row_passes;
        if (row_passes) {
            rows_passed++;
            for (size_t ci = 0; ci < constraints_.size(); ++ci) {
                last_pass_idx[ci] = i;
            }
        } else {
            rows_filtered++;
        }
    }

    // Build outgoing states
    std::vector<InterRowState> outgoing;
    for (size_t ci = 0; ci < constraints_.size(); ++ci) {
        InterRowState state;
        state.column_name = constraints_[ci].column_name;
        const auto& info = col_infos[ci];

        // Find last passing row
        for (int64_t i = rows - 1; i >= 0; --i) {
            if (pass[i]) {
                state.last_value = info.values[i];
                state.initialized = true;
                break;
            }
        }
        outgoing.push_back(state);
    }

    // Build filtered batch by taking passing rows
    std::shared_ptr<arrow::Table> filtered;
    std::vector<int64_t> passing_indices;
    for (int64_t i = 0; i < rows; ++i) {
        if (pass[i]) passing_indices.push_back(i);
    }

    if (!passing_indices.empty()) {
        // Rebuild each column with only passing rows
        std::vector<std::shared_ptr<arrow::ChunkedArray>> new_columns;
        for (int col = 0; col < batch->num_columns(); ++col) {
            auto chunked = batch->column(col);
            auto f = batch->schema()->field(col);

            if (f->type()->id() == arrow::Type::DOUBLE) {
                arrow::DoubleBuilder builder;
                for (auto idx : passing_indices) {
                    // Get value from flattened data — use the original chunked array
                    int64_t offset = 0;
                    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
                        auto arr = std::static_pointer_cast<arrow::DoubleArray>(chunked->chunk(ci));
                        if (idx < offset + arr->length()) {
                            builder.Append(arr->Value(idx - offset));
                            break;
                        }
                        offset += arr->length();
                    }
                }
                auto arr = *builder.Finish();
                new_columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            } else if (f->type()->id() == arrow::Type::INT64) {
                arrow::Int64Builder builder;
                for (auto idx : passing_indices) {
                    int64_t offset = 0;
                    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
                        auto arr = std::static_pointer_cast<arrow::Int64Array>(chunked->chunk(ci));
                        if (idx < offset + arr->length()) {
                            builder.Append(arr->Value(idx - offset));
                            break;
                        }
                        offset += arr->length();
                    }
                }
                auto arr = *builder.Finish();
                new_columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            } else if (f->type()->id() == arrow::Type::TIMESTAMP) {
                arrow::TimestampBuilder builder(
                    std::static_pointer_cast<arrow::TimestampType>(f->type()),
                    arrow::default_memory_pool());
                for (auto idx : passing_indices) {
                    int64_t offset = 0;
                    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
                        auto arr = std::static_pointer_cast<arrow::Int64Array>(chunked->chunk(ci));
                        if (idx < offset + arr->length()) {
                            builder.Append(arr->Value(idx - offset));
                            break;
                        }
                        offset += arr->length();
                    }
                }
                auto arr = *builder.Finish();
                new_columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            } else {
                // String/other columns: rebuild by copying passing rows
                arrow::StringBuilder builder;
                for (auto idx : passing_indices) {
                    int64_t offset = 0;
                    for (int ci = 0; ci < chunked->num_chunks(); ++ci) {
                        auto arr = std::static_pointer_cast<arrow::StringArray>(chunked->chunk(ci));
                        if (idx < offset + arr->length()) {
                            builder.Append(arr->GetString(idx - offset));
                            break;
                        }
                        offset += arr->length();
                    }
                }
                auto arr = *builder.Finish();
                new_columns.push_back(std::make_shared<arrow::ChunkedArray>(arr));
            }
        }
        filtered = arrow::Table::Make(batch->schema(), new_columns);
    }

    InterRowResult result;
    result.filtered_batch = filtered;
    result.outgoing_states = outgoing;
    result.rows_passed = rows_passed;
    result.rows_filtered = rows_filtered;
    result.filter_rate = rows > 0 ? static_cast<double>(rows_filtered) / static_cast<double>(rows) : 0.0;

    span.set_attribute("rows_checked", std::to_string(rows));
    span.set_attribute("rows_filtered", std::to_string(rows_filtered));

    scaffold::MetricsRegistry::instance().counter("inter_row_rows_checked").increment(rows);
    scaffold::MetricsRegistry::instance().counter("inter_row_rows_filtered").increment(rows_filtered);

    return result;
}

scaffold::ExplainInfo InterRowEngine::explain() const {
    scaffold::ExplainInfo info;
    // In a real implementation, populate with constraint details
    return info;
}

}  // namespace synthgen::engine::constraint
