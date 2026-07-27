#pragma once

#include "engine/router/constraint_classifier.h"
#include "common/result.h"
#include "scaffold/explain.h"
#include "schema/schema.h"

#include <string>
#include <optional>

namespace synthgen::engine::router {

enum class DegradationPath {
    kFullFunction,
    kPostFilter,
    kPurePhysics,
    kStatisticalGeneration,
    kKDEPerturbation,
};

struct IdentityDeclaration {
    std::string identity;
    std::string justification;
    DegradationPath path;

    std::string to_string() const {
        return identity + " [" + justification + "]";
    }
};

struct VolumeRatioInfo {
    double constraint_volume = 0.0;
    double data_distribution_volume = 0.0;
    double ratio = 1.0;
    bool estimated = true;
};

struct RoutingDecision {
    DegradationPath selected_path = DegradationPath::kPurePhysics;
    IdentityDeclaration identity;
    ClassificationResult classification;
    VolumeRatioInfo volume_ratio;
    double estimated_exclusion_rate = 0.0;
    bool data_engine_available = false;
    std::string decision_reason;
};

class ExecutionRouter {
public:
    explicit ExecutionRouter(bool data_engine_available = false);

    Result<RoutingDecision> route(
        const ClassificationResult& classification,
        const schema::Schema& schema);

    static const char* identity_for_path(DegradationPath path);

    bool is_data_engine_available() const { return data_engine_available_; }

    scaffold::ExplainInfo explain(const ClassificationResult& classification) const;

private:
    bool data_engine_available_;
};

}  // namespace synthgen::engine::router
