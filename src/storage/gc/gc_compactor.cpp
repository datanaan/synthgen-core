#include "storage/gc/gc_compactor.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <algorithm>
#include <numeric>

namespace synthgen::storage::gc {

GcCompactor::GcCompactor(version::ModelVersionChain& chain,
                          ProtectionChecker& checker,
                          const ProtectionConfig& config)
    : chain_(chain), checker_(checker), config_(config) {}

Result<CompactionResult> GcCompactor::compact(const std::string& model_name) {
    scaffold::SpanGuard span("gc", "compact", "gc_compact");

    // 1. Check in_progress flag
    if (in_progress_.load()) {
        span.set_status("error");
        return Error(ErrorCode::kCompactionInProgress,
                     "compaction already in progress for this compactor",
                     "gc");
    }

    // 2. Set in_progress with RAII reset
    in_progress_ = true;
    struct ProgressGuard {
        std::atomic<bool>& flag;
        ~ProgressGuard() { flag = false; }
    } guard{in_progress_};

    scaffold::MetricsRegistry::instance()
        .counter("gc.compact.attempt")
        .increment();

    // 3. List all versions for this model
    auto versions_result = chain_.list_versions(model_name, 100000);
    if (!versions_result.ok()) {
        span.set_status("error");
        return Error(versions_result.error().code,
                     versions_result.error().message, "gc");
    }

    auto versions = versions_result.value();
    if (versions.empty()) {
        scaffold::MetricsRegistry::instance()
            .counter("gc.compact.empty")
            .increment();
        return CompactionResult{};
    }

    // 4. Sort ascending by created_at (oldest first)
    std::sort(versions.begin(), versions.end(),
              [](const version::ModelVersion& a,
                 const version::ModelVersion& b) {
                  return a.created_at < b.created_at;
              });

    // 5. Compute recent N version IDs (last keep_recent_n entries)
    //    versions is sorted ascending, so the last N are most recent.
    std::vector<std::string> recent_ids;
    int recent_start = std::max(0,
        static_cast<int>(versions.size()) - config_.keep_recent_n);
    for (int i = recent_start; i < static_cast<int>(versions.size()); ++i) {
        recent_ids.push_back(versions[i].version_id);
    }

    // 6. Filter compactable: NOT snapshot_referenced, NOT anchored,
    //    NOT within_n_versions
    std::vector<version::ModelVersion> compactable;
    for (const auto& v : versions) {
        if (checker_.is_snapshot_referenced(v.version_id)) continue;
        if (checker_.is_anchored(v.version_id)) continue;
        if (checker_.is_within_n_versions(v.version_id, recent_ids)) continue;
        compactable.push_back(v);
    }

    // 7. If fewer than 2 compactable versions, nothing to merge
    if (compactable.size() < 2) {
        scaffold::MetricsRegistry::instance()
            .counter("gc.compact.too_few")
            .increment();
        return CompactionResult{};
    }

    // 8. Merge versions
    auto merge_result = merge_versions(compactable);
    if (!merge_result.ok()) {
        span.set_status("error");
        return Error(merge_result.error().code,
                     merge_result.error().message, "gc");
    }

    // 9. Build result
    CompactionResult result;
    for (const auto& v : compactable) {
        result.compacted_versions.push_back(v.version_id);
    }
    result.merged_version_id = merge_result.value().version_id;

    span.set_attribute("compacted_count",
                       std::to_string(result.compacted_versions.size()));
    span.set_attribute("merged_version_id", result.merged_version_id);

    scaffold::MetricsRegistry::instance()
        .counter("gc.compact.success")
        .increment();
    scaffold::MetricsRegistry::instance()
        .gauge("gc.compact.last_compacted_count")
        .set(static_cast<double>(result.compacted_versions.size()));

    return result;
}

Result<void> GcCompactor::auto_compact_check() {
    scaffold::SpanGuard span("gc", "auto_compact_check", "gc_auto");

    if (!config_.auto_compact_enabled) {
        span.set_status("error");
        return Error(ErrorCode::kAutoCompactDisabled,
                     "auto compaction is disabled in config", "gc");
    }
    // Placeholder for timer-based integration
    return {};
}

GcExplainInfo GcCompactor::explain(const std::string& model_name) const {
    GcExplainInfo info;

    auto versions_result = chain_.list_versions(model_name, 100000);
    if (!versions_result.ok() || versions_result.value().empty()) {
        return info;
    }

    auto versions = versions_result.value();
    info.total_versions = static_cast<int>(versions.size());

    // Sort ascending by created_at
    std::sort(versions.begin(), versions.end(),
              [](const version::ModelVersion& a,
                 const version::ModelVersion& b) {
                  return a.created_at < b.created_at;
              });

    // Compute recent N
    std::vector<std::string> recent_ids;
    int recent_start = std::max(0,
        static_cast<int>(versions.size()) - config_.keep_recent_n);
    for (int i = recent_start; i < static_cast<int>(versions.size()); ++i) {
        recent_ids.push_back(versions[i].version_id);
    }

    int protected_count = 0;
    int compactable_count = 0;
    for (const auto& v : versions) {
        bool is_prot = checker_.is_snapshot_referenced(v.version_id) ||
                       checker_.is_anchored(v.version_id) ||
                       checker_.is_within_n_versions(v.version_id, recent_ids);
        if (is_prot) {
            ++protected_count;
        } else {
            ++compactable_count;
        }
    }

    info.protected_versions = protected_count;
    info.compactable_versions = compactable_count;
    return info;
}

Result<version::ModelVersion> GcCompactor::merge_versions(
    const std::vector<version::ModelVersion>& versions) {
    if (versions.empty()) {
        return Error(ErrorCode::kCompactionFailed,
                     "cannot merge empty version list", "gc");
    }

    // fidelity_score = min of all
    double min_fidelity = versions[0].fidelity_score;
    int64_t total_rows = 0;
    std::map<std::string, std::string> merged_metadata;

    for (const auto& v : versions) {
        min_fidelity = std::min(min_fidelity, v.fidelity_score);
        total_rows += v.training_rows;
        // Merge custom_metadata: latter overwrites earlier
        for (const auto& [k, val] : v.custom_metadata) {
            merged_metadata[k] = val;
        }
    }

    // parent_version_id = earliest's parent
    const auto& earliest = versions.front();

    // Build metadata for the new merged version
    version::ModelVersion meta;
    meta.fidelity_score = min_fidelity;
    meta.training_rows = total_rows;
    meta.custom_metadata = merged_metadata;
    meta.training_data_range = earliest.training_data_range;
    meta.created_by = "auto_compact";

    auto result = chain_.create_version(
        earliest.model_name, earliest.parent_version_id, meta);
    if (!result.ok()) {
        return Error(result.error().code,
                     "failed to create merged version: " + result.error().message,
                     "gc");
    }

    return result.value();
}

}  // namespace synthgen::storage::gc
