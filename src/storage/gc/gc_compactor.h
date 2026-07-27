#pragma once

#include "storage/gc/protection.h"
#include "storage/gc/compaction_bias_report.h"
#include "storage/version/model_version_chain.h"
#include "scaffold/explain.h"

#include <string>
#include <vector>
#include <atomic>

namespace synthgen::storage::gc {

struct GcExplainInfo {
    int total_versions = 0;
    int protected_versions = 0;
    int compactable_versions = 0;
};

struct CompactionResult {
    std::vector<std::string> compacted_versions;
    std::string merged_version_id;
};

class GcCompactor {
public:
    GcCompactor(version::ModelVersionChain& chain,
                 ProtectionChecker& checker,
                 const ProtectionConfig& config);

    Result<CompactionResult> compact(const std::string& model_name);
    Result<void> auto_compact_check();
    GcExplainInfo explain(const std::string& model_name) const;

    void set_in_progress_for_test(bool val) { in_progress_ = val; }

private:
    version::ModelVersionChain& chain_;
    ProtectionChecker& checker_;
    ProtectionConfig config_;
    std::atomic<bool> in_progress_{false};

    Result<version::ModelVersion> merge_versions(
        const std::vector<version::ModelVersion>& versions);
};

}  // namespace synthgen::storage::gc
