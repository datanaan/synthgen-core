#pragma once

#include "storage/version/model_version.h"
#include "storage/metadata.h"
#include "scaffold/explain.h"

#include <chrono>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace synthgen::storage::version {

class ModelVersionChain {
public:
    explicit ModelVersionChain(storage::MetadataManager& meta);

    Result<ModelVersion> create_version(
        const std::string& model_name,
        const std::string& parent_version_id,
        const ModelVersion& metadata);

    Result<const ModelVersion*> get_version(const std::string& version_id) const;

    Result<std::vector<ModelVersion>> list_versions(
        const std::string& model_name,
        int limit = 100) const;

    // Immutable guarantee: always returns kImmutableViolation
    Result<void> modify_version(const std::string& version_id);

    scaffold::ExplainInfo explain() const;

private:
    storage::MetadataManager& meta_;
    std::unordered_map<std::string, ModelVersion> versions_;
    std::unordered_map<std::string, std::vector<std::string>> model_versions_;

    std::string generate_version_id();
    bool has_cycle(const std::string& new_id, const std::string& parent_id) const;
};

}  // namespace synthgen::storage::version
