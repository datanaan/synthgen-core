#pragma once

#include <string>
#include <cstdint>

namespace synthgen::engine::evidence {

struct TailReportV3 {
    // Base fields
    std::string epistemological_bias = "physical_first";
    std::string tail_exclusion_statement;
    double exclusion_rate = 0.0;

    // v3 new fields
    std::string rate_band;       // "low" / "medium" / "high" / "critical"
    std::string data_grade;      // from rate_band mapping
    bool fidelity_mismatch = false;
    std::string mismatch_reason;
    std::string compensation_status;  // converging/converged/diverging/timeout_degraded
    int64_t compensation_deadline = 0;
};

// Convert exclusion rate to band
std::string exclusion_rate_to_band(double rate);

// Convert band to data_grade
std::string rate_band_to_data_grade(const std::string& band);

// Build full TailReportV3
TailReportV3 build_tail_report_v3(
    double exclusion_rate,
    bool was_degraded,
    const std::string& compensation_status,
    int64_t compensation_deadline);

}  // namespace synthgen::engine::evidence
