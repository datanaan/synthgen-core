#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/schema_validator.h"
#include "engine/evidence/evidence_package_json.h"
#include "common/hash.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <sstream>

namespace synthgen::engine::evidence {

Result<EvidencePackageV1> EvidencePackageBuilder::build(
    const physics::GenerationResult& generation_result,
    const constraint::ValidationResult& validation_result,
    const TailReportV1& tail_report,
    const ProvenanceV1& provenance,
    const schema::Schema& schema) {

    scaffold::SpanGuard span("evidence", "build", "evp_build");

    scaffold::MetricsRegistry::instance().counter("evidence_package_total").increment();

    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = compute_schema_hash(schema);

    // constraint_summary — extract value ranges from schema
    pkg.constraint_summary.type = "value_range";
    for (const auto& col : schema.columns) {
        if (col.range_min.has_value() && col.range_max.has_value()) {
            pkg.constraint_summary.details.push_back(
                {col.name, col.range_min.value(), col.range_max.value()});
        }
    }

    pkg.exclusion_rate = generation_result.stats.exclusion_rate;
    pkg.data_grade = tail_report.data_grade;
    pkg.row_count = generation_result.data ? generation_result.data->num_rows() : 0;
    pkg.provenance = provenance;

    // conservative_tail_report fields
    pkg.epistemological_bias = tail_report.epistemological_bias;
    pkg.tail_exclusion_statement = tail_report.tail_exclusion_statement;
    pkg.exclusion_rate_report = tail_report.total_exclusion_rate;
    pkg.rows_generated = tail_report.rows_generated;
    pkg.rows_validated = tail_report.rows_validated;
    pkg.rows_failed_validation = tail_report.rows_failed_validation;
    pkg.distribution_used = tail_report.distribution_used;
    pkg.seed_used = tail_report.seed_used;

    // Applicability fields — v1 honest declarations
    pkg.audit_immutability = "not_applicable";
    pkg.statistical_fidelity = "not_applicable";
    pkg.drift_detection = "not_applicable";
    pkg.constraint_type_breakdown = "not_applicable";

    auto vr = validate_schema(pkg);
    if (!vr.ok()) {
        scaffold::MetricsRegistry::instance().counter("evidence_package_errors").increment();
        span.set_status("error");
        return vr.error();
    }

    span.set_attribute("row_count", std::to_string(pkg.row_count));
    return pkg;
}

Result<void> EvidencePackageBuilder::validate_schema(const EvidencePackageV1& pkg) const {
    scaffold::SpanGuard span("evidence", "validate", "evp_validate");
    SchemaValidator validator;
    return validator.validate(pkg);
}

Result<std::string> EvidencePackageBuilder::to_json(const EvidencePackageV1& pkg) const {
    return evidence::to_json(pkg);
}

Result<EvidencePackageV1> EvidencePackageBuilder::from_json(const std::string& json_str) const {
    return evidence::from_json(json_str);
}

std::string EvidencePackageBuilder::compute_schema_hash(const schema::Schema& schema) {
    std::ostringstream oss;
    oss << schema.type_name << "{";
    for (const auto& col : schema.columns) {
        oss << col.name << ":" << static_cast<int>(col.type);
        if (col.range_min) oss << "[" << *col.range_min;
        if (col.range_max) oss << "," << *col.range_max << "]";
        oss << ";";
    }
    oss << "}";
    return sha256_hex(oss.str());
}

}  // namespace synthgen::engine::evidence
