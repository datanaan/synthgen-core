#include "engine/evidence/tail_report_v3.h"
#include <sstream>

namespace synthgen::engine::evidence {

std::string exclusion_rate_to_band(double rate) {
    if (rate < 0.0 || rate > 1.0) {
        // Handle invalid rates by treating as critical
        return "critical";
    }

    if (rate < 0.30) {
        return "low";
    } else if (rate < 0.70) {
        return "medium";
    } else if (rate < 0.90) {
        return "high";
    } else {
        return "critical";
    }
}

std::string rate_band_to_data_grade(const std::string& band) {
    if (band == "low") {
        return "statistics_guaranteed";
    } else if (band == "medium") {
        return "limited_fidelity";
    } else if (band == "high") {
        return "limited_fidelity_conservative";
    } else if (band == "critical") {
        return "rejected";
    }
    // Default for unknown bands
    return "rejected";
}

TailReportV3 build_tail_report_v3(
    double exclusion_rate,
    bool was_degraded,
    const std::string& compensation_status,
    int64_t compensation_deadline) {

    TailReportV3 report;

    // Base fields
    report.epistemological_bias = "physical_first";
    report.exclusion_rate = exclusion_rate;

    // Generate tail_exclusion_statement
    std::ostringstream oss;
    oss << "Excluded " << static_cast<int>(exclusion_rate * 100.0)
        << "% of synthetic data from tail region due to physical constraints.";
    report.tail_exclusion_statement = oss.str();

    // v3 new fields
    report.rate_band = exclusion_rate_to_band(exclusion_rate);
    report.data_grade = rate_band_to_data_grade(report.rate_band);

    // Fidelity mismatch marking
    if (was_degraded) {
        report.fidelity_mismatch = true;
        report.mismatch_reason = "compaction_degraded";
    } else {
        report.fidelity_mismatch = false;
        report.mismatch_reason.clear();
    }

    // Compensation status
    report.compensation_status = compensation_status;
    report.compensation_deadline = compensation_deadline;

    return report;
}

}  // namespace synthgen::engine::evidence
