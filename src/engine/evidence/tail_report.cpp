#include "engine/evidence/tail_report.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

namespace synthgen::engine::evidence {

Result<TailReportV1> TailReportBuilder::build(
    const physics::GenerationResult& generation_result,
    const constraint::ValidationResult& validation_result,
    const physics::GenerationRequest& request,
    const std::vector<parser::ast::ConstraintItem>& constraints) {

    scaffold::SpanGuard span("tail_report", "build", "report-0");

    TailReportV1 report;
    report.rows_generated = generation_result.stats.rows_generated;
    report.rows_validated = validation_result.rows_checked;
    report.rows_failed_validation = validation_result.rows_failed;
    report.distribution_used = generation_result.stats.distribution_used;
    report.seed_used = request.seed;
    report.total_exclusion_rate = generation_result.stats.exclusion_rate;

    // Calculate per-constraint exclusion rates
    // exclusion_rate = 1 - (constraint_width / schema_width)
    // For pure physics path, this should be 0 since all generated values are within bounds
    for (const auto& c : constraints) {
        ConstraintExclusionRate rate;
        rate.constraint_name = "constraint";
        rate.column_name = c.column_name;

        // For BETWEEN constraints
        if (c.op == parser::ast::ConstraintOperator::kBetween) {
            rate.range_width = c.value_max - c.value_min;
            // Estimate schema range from constraint + margin
            rate.schema_range_width = rate.range_width * 2.0;  // Conservative estimate
            rate.rate = 1.0 - (rate.range_width / rate.schema_range_width);
        } else {
            rate.rate = 0.0;
        }

        // For pure physics path, actual exclusion rate from validation
        if (validation_result.rows_checked > 0) {
            rate.rate = static_cast<double>(validation_result.rows_failed) /
                        validation_result.rows_checked;
        }

        report.exclusion_rate_by_constraint.push_back(std::move(rate));
    }

    return report;
}

}  // namespace synthgen::engine::evidence
