#pragma once
#include "common/result.h"
#include "common/types.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include <string>
#include <vector>

namespace synthgen::engine::evidence {

struct ConstraintExclusionRate {
    std::string constraint_name;
    std::string column_name;
    double rate = 0.0;
    double range_width = 0.0;
    double schema_range_width = 0.0;
};

struct TailReportV1 {
    std::string epistemological_bias = "physical_first";
    std::string tail_exclusion_statement =
        "Tail events systematically excluded by value range constraints. "
        "The generated data world's risk spectrum is narrower than the real physical world.";

    std::vector<ConstraintExclusionRate> exclusion_rate_by_constraint;
    double total_exclusion_rate = 0.0;
    std::string data_grade = "physics_guaranteed";

    int64_t rows_generated = 0;
    int64_t rows_validated = 0;
    int64_t rows_failed_validation = 0;
    std::string distribution_used;
    uint64_t seed_used = 0;
};

class TailReportBuilder {
public:
    Result<TailReportV1> build(
        const physics::GenerationResult& generation_result,
        const constraint::ValidationResult& validation_result,
        const physics::GenerationRequest& request,
        const std::vector<parser::ast::ConstraintItem>& constraints);
};

}  // namespace synthgen::engine::evidence
