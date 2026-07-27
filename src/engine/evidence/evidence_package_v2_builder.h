#pragma once

#include "engine/evidence/evidence_package_v2.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "common/result.h"
#include "schema/schema.h"

namespace synthgen::engine::evidence {

class EvidencePackageV2Builder {
public:
    Result<EvidencePackageV2> build(
        int64_t row_count,
        double exclusion_rate,
        const std::string& data_grade,
        const router::RoutingDecision& routing_decision,
        const router::ClassificationResult& classification,
        const postfilter::PostFilterResult& postfilter_result,
        const schema::Schema& schema);

    Result<std::string> to_json(const EvidencePackageV2& pkg);
    Result<EvidencePackageV2> from_json(const std::string& json_str);
};

}  // namespace synthgen::engine::evidence
