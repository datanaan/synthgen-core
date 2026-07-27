#include "engine/router/constraint_classifier.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

namespace synthgen::engine::router {

Result<ClassificationResult> ConstraintClassifier::classify(
    const ConstraintSet& constraints,
    const schema::Schema& schema) {

    scaffold::SpanGuard span("classifier", "classify", "cls");

    ClassificationResult result;
    int vr_count = static_cast<int>(constraints.value_range_names.size());
    int ir_count = static_cast<int>(constraints.inter_row_defs.size());
    int agg_count = static_cast<int>(constraints.aggregate_defs.size());

    // Check for empty
    if (vr_count + ir_count + agg_count == 0) {
        return Error(ErrorCode::kInvalidArgument,
                     "Empty constraint set", "classifier");
    }

    // Validate inter-row constraints: need ORDER column
    if (ir_count > 0) {
        bool has_order = false;
        for (const auto& col : schema.columns) {
            if (col.is_order) { has_order = true; break; }
        }
        if (!has_order) {
            return Error(ErrorCode::kOrderColumnRequired,
                         "Inter-row constraints require an ORDER column",
                         "classifier");
        }
    }

    // Validate aggregate constraints: need ORDER column as DATETIME
    if (agg_count > 0) {
        bool has_order = false;
        for (const auto& col : schema.columns) {
            if (col.is_order) {
                has_order = true;
                if (col.type != DataType::kDatetime) {
                    return Error(ErrorCode::kTypeMismatch,
                                 "Aggregate constraints require ORDER column of DATETIME type",
                                 "classifier");
                }
                break;
            }
        }
        if (!has_order) {
            return Error(ErrorCode::kOrderColumnRequired,
                         "Aggregate constraints require an ORDER column",
                         "classifier");
        }
    }

    // Classify value range constraints
    for (const auto& name : constraints.value_range_names) {
        ConstraintClassification cc;
        cc.constraint_name = name;
        cc.type = ConstraintType::kValueRange;
        cc.phase = ExecutionPhase::kPhaseOne;
        result.classifications.push_back(cc);
    }

    // Classify inter-row constraints
    for (const auto& def : constraints.inter_row_defs) {
        ConstraintClassification cc;
        cc.constraint_name = def.column_name + "_inter_row";
        cc.type = ConstraintType::kInterRow;
        cc.phase = ExecutionPhase::kPhaseOne;
        result.classifications.push_back(cc);
    }

    // Classify aggregate constraints
    for (const auto& def : constraints.aggregate_defs) {
        ConstraintClassification cc;
        cc.constraint_name = def.constraint_name;
        cc.type = ConstraintType::kAggregate;
        cc.phase = ExecutionPhase::kPhaseTwo;
        result.classifications.push_back(cc);
    }

    result.value_range_count = vr_count;
    result.inter_row_count = ir_count;
    result.aggregate_count = agg_count;
    result.execution_mode = derive_execution_mode(vr_count, ir_count, agg_count);

    scaffold::MetricsRegistry::instance().counter("classifier_total").increment();

    return result;
}

ExecutionMode ConstraintClassifier::derive_execution_mode(
    int value_range_count, int inter_row_count, int aggregate_count) {

    if (aggregate_count > 0) return ExecutionMode::kTwoPhase;
    if (inter_row_count > 0) return ExecutionMode::kStatefulBatch;
    return ExecutionMode::kRowByRow;
}

scaffold::ExplainInfo ConstraintClassifier::explain(
    const ClassificationResult& result) const {
    scaffold::ExplainInfo info;
    return info;
}

}  // namespace synthgen::engine::router
