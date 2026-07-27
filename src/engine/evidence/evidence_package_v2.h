#pragma once

#include "engine/evidence/evidence_package.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"

#include <string>
#include <vector>
#include <optional>

namespace synthgen::engine::evidence {

// v2 new structures
struct StatisticalFidelity {
    bool available = false;
    std::string model_version;
    double fidelity_score = 0.0;
    int64_t training_rows = 0;
};

struct ConstraintTypeBreakdown {
    int value_range_count = 0;
    int inter_row_count = 0;
    int aggregate_count = 0;
};

struct PostFilterInfo {
    bool was_post_filtered = false;
    int64_t pre_filter_rows = 0;
    int64_t post_filter_rows = 0;
    double actual_exclusion_rate = 0.0;
    std::string exclusion_rate_band;
    bool was_timeout_truncated = false;
};

struct DataEngineInfo {
    std::string model_version;
    int dimensions = 0;
    double bandwidth = 0.0;
    double volume_ratio = 0.0;
};

struct ProvenanceV2 {
    ProvenanceV1 base;
    std::string degradation_path;
    router::IdentityDeclaration identity;
    std::optional<DataEngineInfo> data_engine_info;
};

struct EvidencePackageV2 {
    // Inherited from v1
    std::string schema_version = "v2";
    std::string schema_hash;
    ConstraintSummary constraint_summary;
    double exclusion_rate = 0.0;
    std::string data_grade = "physics_guaranteed";
    int64_t row_count = 0;
    ProvenanceV2 provenance;
    // Tail report
    std::string epistemological_bias = "physical_first";
    std::string tail_exclusion_statement;
    double exclusion_rate_report = 0.0;
    int64_t rows_generated = 0;
    int64_t rows_validated = 0;
    int64_t rows_failed_validation = 0;
    std::string distribution_used;
    uint64_t seed_used = 0;

    // v2 new fields
    std::string audit_immutability = "verified";
    StatisticalFidelity statistical_fidelity;
    ConstraintTypeBreakdown constraint_type_breakdown;
    router::IdentityDeclaration generator_identity;
    PostFilterInfo post_filter_info;
};

}  // namespace synthgen::engine::evidence
