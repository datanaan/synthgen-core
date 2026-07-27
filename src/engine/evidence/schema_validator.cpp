#include "engine/evidence/schema_validator.h"
#include <cmath>

namespace synthgen::engine::evidence {

Result<void> SchemaValidator::validate(const EvidencePackageV1& pkg) const {
    auto r1 = validate_required_fields(pkg);
    if (!r1.ok()) return r1;

    auto r2 = validate_applicability(pkg);
    if (!r2.ok()) return r2;

    auto r3 = validate_honesty(pkg);
    if (!r3.ok()) return r3;

    return {};
}

Result<void> SchemaValidator::validate_required_fields(const EvidencePackageV1& pkg) const {
    // schema_version must not be empty
    if (pkg.schema_version.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "schema_version is required", "schema_validator");
    }
    // schema_hash must not be empty
    if (pkg.schema_hash.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "schema_hash is required", "schema_validator");
    }
    // data_grade must not be empty
    if (pkg.data_grade.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "data_grade is required", "schema_validator");
    }
    // exclusion_rate must not be NaN
    if (std::isnan(pkg.exclusion_rate)) {
        return Error(ErrorCode::kSchemaViolation,
                     "exclusion_rate must not be NaN", "schema_validator");
    }
    // row_count must be >= 0
    if (pkg.row_count < 0) {
        return Error(ErrorCode::kSchemaViolation,
                     "row_count must be >= 0", "schema_validator");
    }
    // epistemological_bias must not be empty
    if (pkg.epistemological_bias.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "epistemological_bias is required", "schema_validator");
    }
    // tail_exclusion_statement must not be empty
    if (pkg.tail_exclusion_statement.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "tail_exclusion_statement is required", "schema_validator");
    }
    // applicability fields must not be empty
    if (pkg.audit_immutability.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "audit_immutability is required", "schema_validator");
    }
    if (pkg.statistical_fidelity.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "statistical_fidelity is required", "schema_validator");
    }
    if (pkg.drift_detection.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "drift_detection is required", "schema_validator");
    }
    if (pkg.constraint_type_breakdown.empty()) {
        return Error(ErrorCode::kSchemaViolation,
                     "constraint_type_breakdown is required", "schema_validator");
    }
    return {};
}

Result<void> SchemaValidator::validate_applicability(const EvidencePackageV1& pkg) const {
    if (pkg.audit_immutability != "not_applicable") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 audit_immutability must be 'not_applicable'",
                     "schema_validator");
    }
    if (pkg.statistical_fidelity != "not_applicable") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 statistical_fidelity must be 'not_applicable'",
                     "schema_validator");
    }
    if (pkg.drift_detection != "not_applicable") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 drift_detection must be 'not_applicable'",
                     "schema_validator");
    }
    if (pkg.constraint_type_breakdown != "not_applicable") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 constraint_type_breakdown must be 'not_applicable'",
                     "schema_validator");
    }
    return {};
}

Result<void> SchemaValidator::validate_honesty(const EvidencePackageV1& pkg) const {
    // epistemological_bias must be "physical_first" in v1
    if (pkg.epistemological_bias != "physical_first") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 epistemological_bias must be 'physical_first'",
                     "schema_validator");
    }
    // tail_exclusion_statement must not be empty
    if (pkg.tail_exclusion_statement.empty()) {
        return Error(ErrorCode::kHonestyViolation,
                     "tail_exclusion_statement must not be empty",
                     "schema_validator");
    }
    // data_grade must be "physics_guaranteed" in v1
    if (pkg.data_grade != "physics_guaranteed") {
        return Error(ErrorCode::kHonestyViolation,
                     "v1 data_grade must be 'physics_guaranteed'",
                     "schema_validator");
    }
    // Pure physics path: exclusion_rate must be effectively 0.0
    constexpr double kExclusionEpsilon = 1e-10;
    if (std::abs(pkg.exclusion_rate) > kExclusionEpsilon) {
        return Error(ErrorCode::kConsistencyError,
                     "v1 pure physics path exclusion_rate must be 0.0, got " +
                     std::to_string(pkg.exclusion_rate),
                     "schema_validator");
    }
    // rows_failed_validation must be 0 in v1
    if (pkg.rows_failed_validation != 0) {
        return Error(ErrorCode::kConsistencyError,
                     "v1 rows_failed_validation must be 0, got " +
                     std::to_string(pkg.rows_failed_validation),
                     "schema_validator");
    }
    return {};
}

}  // namespace synthgen::engine::evidence
