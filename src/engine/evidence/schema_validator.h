#pragma once

#include "engine/evidence/evidence_package.h"
#include "common/result.h"

namespace synthgen::engine::evidence {

class SchemaValidator {
public:
    Result<void> validate(const EvidencePackageV1& pkg) const;

private:
    Result<void> validate_required_fields(const EvidencePackageV1& pkg) const;
    Result<void> validate_applicability(const EvidencePackageV1& pkg) const;
    Result<void> validate_honesty(const EvidencePackageV1& pkg) const;
};

}  // namespace synthgen::engine::evidence
