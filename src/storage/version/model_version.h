#pragma once

#include "common/types.h"

#include <map>
#include <string>

namespace synthgen::storage::version {

struct ModelVersion {
    std::string version_id;
    std::string model_name;
    std::string parent_version_id;
    Timestamp created_at = 0;
    std::string created_by;  // "user" / "system" / "auto_compact"
    bool is_immutable = true;

    // Model metadata
    std::string training_data_range;
    double fidelity_score = 0.0;
    int64_t training_rows = 0;
    std::map<std::string, std::string> custom_metadata;

    bool is_first_version() const { return parent_version_id.empty(); }
};

}  // namespace synthgen::storage::version
