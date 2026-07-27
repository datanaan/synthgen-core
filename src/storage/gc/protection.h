#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace synthgen::storage::gc {

struct ProtectionConfig {
    int keep_recent_n = 10;
    bool auto_compact_enabled = true;
};

class ProtectionChecker {
public:
    explicit ProtectionChecker(const ProtectionConfig& config = {});

    bool is_protected(const std::string& version_id) const;
    bool is_snapshot_referenced(const std::string& version_id) const;
    bool is_anchored(const std::string& version_id) const;
    bool is_within_n_versions(
        const std::string& version_id,
        const std::vector<std::string>& recent_version_ids) const;

    void add_snapshot_ref(const std::string& version_id);
    void remove_snapshot_ref(const std::string& version_id);
    void anchor(const std::string& version_id);
    void unanchor(const std::string& version_id);

private:
    ProtectionConfig config_;
    std::unordered_set<std::string> snapshot_refs_;
    std::unordered_set<std::string> anchored_;
};

}  // namespace synthgen::storage::gc
