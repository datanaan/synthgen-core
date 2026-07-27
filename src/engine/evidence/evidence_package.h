#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace synthgen::engine::evidence {

struct ConstraintDetail {
    std::string column;
    double min = 0.0;
    double max = 0.0;
};

struct ConstraintSummary {
    std::string type = "value_range";
    std::vector<ConstraintDetail> details;
};

struct GenerationParams {
    uint64_t seed = 0;
    std::string distribution = "uniform";
    int64_t limit = 0;
    int64_t batch_size = 1000;
};

struct TraceSpanEntry {
    std::string trace_id;
    std::string span_id;
    std::string component;
    std::string operation;
    std::string status = "ok";
};

struct ProvenanceV1 {
    std::string data_source;
    std::vector<std::string> constraints;
    GenerationParams generation_params;
    std::vector<TraceSpanEntry> trace_spans;
    std::string generator_identity;
};

struct EvidencePackageV1 {
    std::string schema_version = "v1";
    std::string schema_hash;
    ConstraintSummary constraint_summary;
    double exclusion_rate = 0.0;
    std::string data_grade = "physics_guaranteed";
    int64_t row_count = 0;
    ProvenanceV1 provenance;
    // conservative_tail_report fields (inline for v1)
    std::string epistemological_bias = "physical_first";
    std::string tail_exclusion_statement;
    double exclusion_rate_report = 0.0;
    int64_t rows_generated = 0;
    int64_t rows_validated = 0;
    int64_t rows_failed_validation = 0;
    std::string distribution_used;
    uint64_t seed_used = 0;
    // Applicability fields
    std::string audit_immutability = "not_applicable";
    std::string statistical_fidelity = "not_applicable";
    std::string drift_detection = "not_applicable";
    std::string constraint_type_breakdown = "not_applicable";
};

}  // namespace synthgen::engine::evidence
