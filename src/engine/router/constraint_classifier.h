#pragma once

#include "common/result.h"
#include "common/types.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "scaffold/explain.h"
#include "schema/schema.h"

#include <string>
#include <vector>

namespace synthgen::engine::router {

enum class ConstraintType {
    kValueRange,
    kInterRow,
    kAggregate,
};

enum class ExecutionPhase {
    kPhaseOne,
    kPhaseTwo,
};

enum class ExecutionMode {
    kRowByRow,
    kStatefulBatch,
    kTwoPhase,
};

struct ConstraintClassification {
    std::string constraint_name;
    ConstraintType type;
    ExecutionPhase phase;
};

struct ClassificationResult {
    std::vector<ConstraintClassification> classifications;
    ExecutionMode execution_mode = ExecutionMode::kRowByRow;
    int value_range_count = 0;
    int inter_row_count = 0;
    int aggregate_count = 0;

    bool has_inter_row() const { return inter_row_count > 0; }
    bool has_aggregate() const { return aggregate_count > 0; }

    std::vector<ConstraintClassification> phase_one_constraints() const {
        std::vector<ConstraintClassification> result;
        for (const auto& c : classifications) {
            if (c.phase == ExecutionPhase::kPhaseOne) result.push_back(c);
        }
        return result;
    }

    std::vector<ConstraintClassification> phase_two_constraints() const {
        std::vector<ConstraintClassification> result;
        for (const auto& c : classifications) {
            if (c.phase == ExecutionPhase::kPhaseTwo) result.push_back(c);
        }
        return result;
    }
};

// Input descriptor for classification — describes what constraint types are present
struct ConstraintSet {
    std::vector<std::string> value_range_names;
    std::vector<synthgen::engine::constraint::InterRowConstraintDef> inter_row_defs;
    std::vector<synthgen::engine::constraint::AggregateConstraintDef> aggregate_defs;
};

class ConstraintClassifier {
public:
    Result<ClassificationResult> classify(
        const ConstraintSet& constraints,
        const schema::Schema& schema);

    ExecutionMode derive_execution_mode(
        int value_range_count, int inter_row_count, int aggregate_count);

    scaffold::ExplainInfo explain(const ClassificationResult& result) const;
};

}  // namespace synthgen::engine::router
