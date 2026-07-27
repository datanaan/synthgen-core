#pragma once

#include "engine/evidence/evidence_package.h"
#include "common/result.h"
#include <string>

namespace synthgen::engine::evidence {

Result<std::string> to_json(const EvidencePackageV1& pkg);
Result<EvidencePackageV1> from_json(const std::string& json_str);

}  // namespace synthgen::engine::evidence
