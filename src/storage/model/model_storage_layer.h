#pragma once

#include "common/result.h"
#include "storage/version/model_version.h"

#include <filesystem>
#include <list>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace synthgen::storage::model {

class ModelStorageLayer {
public:
    explicit ModelStorageLayer(const std::string& storage_root);

    Result<void> save_checkpoint(
        const std::string& model_name,
        const std::string& version_id,
        const std::string& model_data);

    Result<std::string> load_model(
        const std::string& model_name,
        const std::string& version_id);

    Result<std::vector<std::string>> list_model_versions(
        const std::string& model_name);

    // atomic_write: three-phase commit
    // Phase 1: write data to .pending file
    // Phase 2: rename .pending to .parquet (final)
    // Phase 3: audit span marker
    Result<void> atomic_write(
        const std::string& model_name,
        const std::string& model_data,
        const version::ModelVersion& version);

    // Recovery from interrupted atomic_write
    Result<void> recover_interrupted();

private:
    std::filesystem::path root_;
    mutable std::mutex cache_mutex_;

    // LRU cache: list stores (key, value) with most-recently-used at front;
    // map provides O(1) lookup by key → iterator into the list.
    using CacheEntry = std::pair<std::string, std::string>;
    std::list<CacheEntry> lru_list_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> lru_map_;
    static constexpr size_t kMaxCacheSize = 5;

    std::filesystem::path model_dir(const std::string& model_name) const;
    std::filesystem::path checkpoint_path(
        const std::string& model_name, const std::string& version_id) const;
    std::filesystem::path pending_path(
        const std::string& model_name, const std::string& version_id) const;

    void update_cache(const std::string& key, const std::string& value);
    std::string cache_key(const std::string& model_name,
                          const std::string& version_id) const;
};

}  // namespace synthgen::storage::model
