#include "storage/version/model_version_chain.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

namespace synthgen::storage::version {

namespace {

int64_t now_us() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch())
        .count();
}

}  // namespace

ModelVersionChain::ModelVersionChain(storage::MetadataManager& meta)
    : meta_(meta) {}

std::string ModelVersionChain::generate_version_id() {
    static std::atomic<uint64_t> counter{0};
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    uint64_t cnt = counter.fetch_add(1);

    // Add random component for uniqueness
    static thread_local std::mt19937_64 rng(std::random_device{}());
    uint64_t rand_val = rng();

    std::ostringstream oss;
    oss << "mv_" << std::hex << ts << "_" << cnt << "_" << rand_val;
    return oss.str();
}

bool ModelVersionChain::has_cycle(const std::string& new_id,
                                   const std::string& parent_id) const {
    // Walk from parent up to root. If we encounter new_id, there's a cycle.
    std::string current = parent_id;
    std::unordered_set<std::string> visited;
    while (!current.empty()) {
        if (current == new_id) return true;
        if (visited.count(current)) return true;  // loop in existing chain
        visited.insert(current);
        auto it = versions_.find(current);
        if (it == versions_.end()) break;
        current = it->second.parent_version_id;
    }
    return false;
}

Result<ModelVersion> ModelVersionChain::create_version(
    const std::string& model_name,
    const std::string& parent_version_id,
    const ModelVersion& metadata) {
    scaffold::SpanGuard span("version", "create_version", "version_create");

    // Validate model_name
    if (model_name.empty()) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("version.create.error")
            .increment();
        return Error(ErrorCode::kInvalidArgument,
                     "model_name must not be empty", "version");
    }

    // Validate parent exists if specified
    if (!parent_version_id.empty()) {
        auto parent_it = versions_.find(parent_version_id);
        if (parent_it == versions_.end()) {
            span.set_status("error");
            scaffold::MetricsRegistry::instance()
                .counter("version.create.error")
                .increment();
            return Error(ErrorCode::kParentNotFound,
                         "parent version not found: " + parent_version_id,
                         "version");
        }
        // Parent must belong to the same model
        if (parent_it->second.model_name != model_name) {
            span.set_status("error");
            scaffold::MetricsRegistry::instance()
                .counter("version.create.error")
                .increment();
            return Error(ErrorCode::kInvalidArgument,
                         "parent version belongs to different model",
                         "version");
        }
    }

    // Generate version ID
    std::string vid = generate_version_id();

    // Check for cycles
    if (has_cycle(vid, parent_version_id)) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("version.create.error")
            .increment();
        return Error(ErrorCode::kVersionChainCycle,
                     "adding this version would create a cycle", "version");
    }

    // Build the version object
    ModelVersion v;
    v.version_id = vid;
    v.model_name = model_name;
    v.parent_version_id = parent_version_id;
    v.created_at = now_us();
    v.created_by = metadata.created_by.empty() ? "user" : metadata.created_by;
    v.is_immutable = true;
    v.training_data_range = metadata.training_data_range;
    v.fidelity_score = metadata.fidelity_score;
    v.training_rows = metadata.training_rows;
    v.custom_metadata = metadata.custom_metadata;

    // Store in maps
    versions_[vid] = v;
    model_versions_[model_name].push_back(vid);

    span.set_attribute("version_id", vid);
    span.set_attribute("model_name", model_name);

    scaffold::MetricsRegistry::instance()
        .counter("version.create.success")
        .increment();

    return v;
}

Result<const ModelVersion*> ModelVersionChain::get_version(
    const std::string& version_id) const {
    scaffold::SpanGuard span("version", "get_version", "version_get");

    auto it = versions_.find(version_id);
    if (it == versions_.end()) {
        span.set_status("error");
        scaffold::MetricsRegistry::instance()
            .counter("version.get.miss")
            .increment();
        return Error(ErrorCode::kVersionNotFound,
                     "version not found: " + version_id, "version");
    }

    scaffold::MetricsRegistry::instance()
        .counter("version.get.hit")
        .increment();
    return &(it->second);
}

Result<std::vector<ModelVersion>> ModelVersionChain::list_versions(
    const std::string& model_name,
    int limit) const {
    scaffold::SpanGuard span("version", "list_versions", "version_list");

    auto it = model_versions_.find(model_name);
    if (it == model_versions_.end()) {
        return std::vector<ModelVersion>{};
    }

    std::vector<ModelVersion> result;
    result.reserve(it->second.size());
    for (const auto& vid : it->second) {
        auto vit = versions_.find(vid);
        if (vit != versions_.end()) {
            result.push_back(vit->second);
        }
    }

    // Sort by created_at descending
    std::sort(result.begin(), result.end(),
              [](const ModelVersion& a, const ModelVersion& b) {
                  return a.created_at > b.created_at;
              });

    // Apply limit
    if (static_cast<int>(result.size()) > limit) {
        result.resize(static_cast<size_t>(limit));
    }

    scaffold::MetricsRegistry::instance()
        .counter("version.list.calls")
        .increment();

    return result;
}

Result<void> ModelVersionChain::modify_version(
    const std::string& /*version_id*/) {
    scaffold::SpanGuard span("version", "modify_version", "version_modify");
    span.set_status("error");

    scaffold::MetricsRegistry::instance()
        .counter("version.modify.rejected")
        .increment();

    return Error(ErrorCode::kImmutableViolation,
                 "model versions are immutable and cannot be modified",
                 "version");
}

scaffold::ExplainInfo ModelVersionChain::explain() const {
    scaffold::ExplainInfo info;
    info.version = "v3";
    info.path = "ModelVersionChain";
    info.supported_statements = {"create_version", "get_version",
                                  "list_versions", "modify_version"};
    info.unsupported_in_v1 = {"modify_version"};
    return info;
}

}  // namespace synthgen::storage::version
