#include "storage/gc/protection.h"

#include <algorithm>

namespace synthgen::storage::gc {

ProtectionChecker::ProtectionChecker(const ProtectionConfig& config)
    : config_(config) {}

bool ProtectionChecker::is_protected(const std::string& version_id) const {
    return is_snapshot_referenced(version_id) || is_anchored(version_id);
}

bool ProtectionChecker::is_snapshot_referenced(const std::string& version_id) const {
    return snapshot_refs_.count(version_id) > 0;
}

bool ProtectionChecker::is_anchored(const std::string& version_id) const {
    return anchored_.count(version_id) > 0;
}

bool ProtectionChecker::is_within_n_versions(
    const std::string& version_id,
    const std::vector<std::string>& recent_version_ids) const {
    // recent_version_ids is expected to be sorted most-recent-first,
    // already trimmed to keep_recent_n size. Just check membership.
    return std::find(recent_version_ids.begin(),
                     recent_version_ids.end(),
                     version_id) != recent_version_ids.end();
}

void ProtectionChecker::add_snapshot_ref(const std::string& version_id) {
    snapshot_refs_.insert(version_id);
}

void ProtectionChecker::remove_snapshot_ref(const std::string& version_id) {
    snapshot_refs_.erase(version_id);
}

void ProtectionChecker::anchor(const std::string& version_id) {
    anchored_.insert(version_id);
}

void ProtectionChecker::unanchor(const std::string& version_id) {
    anchored_.erase(version_id);
}

}  // namespace synthgen::storage::gc
