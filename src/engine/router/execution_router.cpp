#include "engine/router/execution_router.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

namespace synthgen::engine::router {

ExecutionRouter::ExecutionRouter(bool data_engine_available)
    : data_engine_available_(data_engine_available) {}

const char* ExecutionRouter::identity_for_path(DegradationPath path) {
    switch (path) {
        case DegradationPath::kFullFunction:
            return "constraint_driven_synthetic";
        case DegradationPath::kPostFilter:
            return "post_filter_synthetic";
        case DegradationPath::kPurePhysics:
            return "physics_sampler";
        case DegradationPath::kStatisticalGeneration:
            return "statistical_generator";
        case DegradationPath::kKDEPerturbation:
            return "kde_perturbation_generator";
    }
    return "unknown";
}

Result<RoutingDecision> ExecutionRouter::route(
    const ClassificationResult& classification,
    const schema::Schema& schema) {

    scaffold::SpanGuard span("router", "route", "rt_route");

    RoutingDecision decision;
    decision.classification = classification;
    decision.data_engine_available = data_engine_available_;

    // Routing rules (priority from high to low):
    //
    // 1. FullFunction: constraints complete + data engine available
    // 2. PostFilter: exclusion rate < 90%, has non-trivial constraints
    // 3. PurePhysics: only value range constraints OR no data engine
    // 4. StatisticalGeneration: constraints incomplete + data engine
    // 5. KDEPerturbation: extremely incomplete + data engine

    bool has_constraints = (classification.value_range_count +
                           classification.inter_row_count +
                           classification.aggregate_count) > 0;

    if (has_constraints && data_engine_available_ &&
        classification.aggregate_count > 0) {
        // Full constraint coverage with data engine → full function
        decision.selected_path = DegradationPath::kFullFunction;
        decision.decision_reason = "Constraints complete with data engine";
        decision.estimated_exclusion_rate = 0.1;
    } else if (has_constraints && data_engine_available_ &&
               classification.inter_row_count > 0) {
        // Post-filter path for inter-row constraints with data engine
        decision.selected_path = DegradationPath::kPostFilter;
        decision.decision_reason = "Inter-row constraints with post-filtering";
        decision.estimated_exclusion_rate = 0.3;
    } else if (classification.value_range_count > 0 &&
               classification.inter_row_count == 0 &&
               classification.aggregate_count == 0) {
        // Pure value range → pure physics (v1 equivalent)
        decision.selected_path = DegradationPath::kPurePhysics;
        decision.decision_reason = "Value-range only constraints, pure physics path";
        decision.estimated_exclusion_rate = 0.0;
    } else if (data_engine_available_ && !has_constraints) {
        // No constraints but data engine available → statistical generation
        decision.selected_path = DegradationPath::kStatisticalGeneration;
        decision.decision_reason = "No constraints, statistical generation with data engine";
        decision.estimated_exclusion_rate = 0.0;
    } else if (data_engine_available_) {
        // KDE perturbation as fallback
        decision.selected_path = DegradationPath::kKDEPerturbation;
        decision.decision_reason = "KDE perturbation with data engine";
        decision.estimated_exclusion_rate = 0.5;
    } else {
        // No data engine → pure physics always
        decision.selected_path = DegradationPath::kPurePhysics;
        decision.decision_reason = "No data engine available, defaulting to pure physics";
        decision.estimated_exclusion_rate = 0.0;
    }

    // Set identity
    decision.identity.path = decision.selected_path;
    decision.identity.identity = identity_for_path(decision.selected_path);
    decision.identity.justification = decision.decision_reason;

    scaffold::MetricsRegistry::instance().counter("router_total").increment();
    std::string path_str(1, static_cast<char>(decision.selected_path));
    span.set_attribute("path", std::to_string(static_cast<int>(decision.selected_path)));

    return decision;
}

scaffold::ExplainInfo ExecutionRouter::explain(
    const ClassificationResult& classification) const {
    scaffold::ExplainInfo info;
    return info;
}

}  // namespace synthgen::engine::router
