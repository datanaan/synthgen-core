#pragma once

#include "engine/evidence/evidence_package.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "schema/schema.h"
#include "common/result.h"

namespace synthgen::engine::evidence {

class EvidencePackageBuilder {
public:
    Result<EvidencePackageV1> build(
        const physics::GenerationResult& generation_result,
        const constraint::ValidationResult& validation_result,
        const TailReportV1& tail_report,
        const ProvenanceV1& provenance,
        const schema::Schema& schema);

    Result<void> validate_schema(const EvidencePackageV1& pkg) const;
    Result<std::string> to_json(const EvidencePackageV1& pkg) const;
    Result<EvidencePackageV1> from_json(const std::string& json_str) const;

    static std::string compute_schema_hash(const schema::Schema& schema);
};

}  // namespace synthgen::engine::evidence
