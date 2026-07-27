#pragma once

#include "common/result.h"
#include "storage/version/model_version_chain.h"
#include "storage/model/model_storage_layer.h"
#include "storage/gc/compaction_bias_report.h"

#include <string>
#include <optional>

namespace synthgen::storage::timetravel {

struct TimeTravelResult {
    std::string data;
    version::ModelVersion version;
    std::optional<gc::CompactionBiasReport> bias_report;
    bool was_degraded = false;
};

class TimeTravelEngine {
public:
    TimeTravelEngine(version::ModelVersionChain& chain,
                     model::ModelStorageLayer& storage);

    Result<TimeTravelResult> query_as_of(
        const std::string& model_name,
        const std::string& version_id);

private:
    version::ModelVersionChain& chain_;
    model::ModelStorageLayer& storage_;

    Result<std::string> find_nearest_available(
        const std::string& model_name,
        const std::string& requested_version,
        gc::CompactionBiasReport& report);
};

}  // namespace synthgen::storage::timetravel
