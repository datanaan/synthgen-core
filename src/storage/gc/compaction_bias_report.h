#pragma once

#include <string>
#include <vector>

namespace synthgen::storage::gc {

struct CompactionBiasReport {
    std::string requested_version;
    std::string returned_version;
    std::string reason;  // "compacted" / "anchored" / "snapshot_referenced"
    std::vector<std::string> merged_from;
    std::string training_data_range;
    double fidelity_score_range_min = 0.0;
    double fidelity_score_range_max = 0.0;
    bool version_mismatch = false;

    bool empty() const { return requested_version.empty(); }
};

}  // namespace synthgen::storage::gc
