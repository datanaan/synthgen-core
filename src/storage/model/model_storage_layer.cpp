#include "storage/model/model_storage_layer.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <fstream>
#include <sstream>

namespace synthgen::storage::model {

using scaffold::SpanGuard;

ModelStorageLayer::ModelStorageLayer(const std::string& storage_root)
    : root_(storage_root) {
    std::filesystem::create_directories(root_);
}

std::filesystem::path ModelStorageLayer::model_dir(
    const std::string& model_name) const {
    return root_ / "models" / model_name;
}

std::filesystem::path ModelStorageLayer::checkpoint_path(
    const std::string& model_name, const std::string& version_id) const {
    return model_dir(model_name) / (version_id + ".parquet");
}

std::filesystem::path ModelStorageLayer::pending_path(
    const std::string& model_name, const std::string& version_id) const {
    return model_dir(model_name) / (version_id + ".pending");
}

std::string ModelStorageLayer::cache_key(const std::string& model_name,
                                         const std::string& version_id) const {
    return model_name + "/" + version_id;
}

void ModelStorageLayer::update_cache(const std::string& key,
                                     const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = lru_map_.find(key);
    if (it != lru_map_.end()) {
        // Key exists: move to front (most recently used)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        it->second->second = value;
        return;
    }

    // Evict least-recently-used (back of list) if at capacity
    if (lru_list_.size() >= kMaxCacheSize) {
        auto& back = lru_list_.back();
        lru_map_.erase(back.first);
        lru_list_.pop_back();
    }

    // Insert at front
    lru_list_.push_front({key, value});
    lru_map_[key] = lru_list_.begin();
}

Result<void> ModelStorageLayer::save_checkpoint(
    const std::string& model_name,
    const std::string& version_id,
    const std::string& model_data) {
    SpanGuard span("ModelStorageLayer", "save_checkpoint", "");

    auto dir = model_dir(model_name);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Error(ErrorCode::kWriteFailed,
                     "Failed to create model directory: " + ec.message(),
                     "ModelStorageLayer");
    }

    auto path = checkpoint_path(model_name, version_id);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Error(ErrorCode::kWriteFailed,
                     "Failed to open checkpoint file for writing",
                     "ModelStorageLayer");
    }
    out.write(model_data.data(), static_cast<std::streamsize>(model_data.size()));
    out.close();
    if (out.fail()) {
        return Error(ErrorCode::kWriteFailed,
                     "Failed to write checkpoint data",
                     "ModelStorageLayer");
    }

    update_cache(cache_key(model_name, version_id), model_data);

    scaffold::MetricsRegistry::instance()
        .counter("model_storage.save_checkpoint")
        .increment();
    scaffold::MetricsRegistry::instance()
        .histogram("model_storage.write_bytes")
        .observe(static_cast<double>(model_data.size()));
    return {};
}

Result<std::string> ModelStorageLayer::load_model(
    const std::string& model_name,
    const std::string& version_id) {
    SpanGuard span("ModelStorageLayer", "load_model", "");

    // Check cache first, but validate that the file still exists on disk.
    // This prevents stale cache hits after external file deletion (e.g. compaction).
    auto key = cache_key(model_name, version_id);
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = lru_map_.find(key);
        if (it != lru_map_.end()) {
            auto path = checkpoint_path(model_name, version_id);
            if (std::filesystem::exists(path)) {
                // Cache hit: move to front
                lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
                span.set_attribute("cache", "hit");
                return it->second->second;
            }
            // File was deleted behind our back — invalidate stale cache entry
            lru_list_.erase(it->second);
            lru_map_.erase(it);
            span.set_attribute("cache", "invalidated");
        }
    }
    span.set_attribute("cache", "miss");

    // Read from filesystem
    auto path = checkpoint_path(model_name, version_id);
    if (!std::filesystem::exists(path)) {
        return Error(ErrorCode::kVersionNotFound,
                     "Version not found: " + version_id,
                     "ModelStorageLayer");
    }

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return Error(ErrorCode::kReadFailed,
                     "Failed to open checkpoint file for reading",
                     "ModelStorageLayer");
    }

    auto size = in.tellg();
    in.seekg(0);
    std::string data(static_cast<size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    in.close();
    if (in.fail()) {
        return Error(ErrorCode::kReadFailed,
                     "Failed to read checkpoint data",
                     "ModelStorageLayer");
    }

    update_cache(key, data);

    scaffold::MetricsRegistry::instance()
        .counter("model_storage.load_model")
        .increment();
    scaffold::MetricsRegistry::instance()
        .histogram("model_storage.read_bytes")
        .observe(static_cast<double>(data.size()));
    return data;
}

Result<std::vector<std::string>> ModelStorageLayer::list_model_versions(
    const std::string& model_name) {
    SpanGuard span("ModelStorageLayer", "list_model_versions", "");

    std::vector<std::string> versions;
    auto dir = model_dir(model_name);

    if (!std::filesystem::exists(dir)) {
        return versions;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".parquet") {
            versions.push_back(entry.path().stem().string());
        }
    }

    scaffold::MetricsRegistry::instance()
        .counter("model_storage.list_versions")
        .increment();
    return versions;
}

Result<void> ModelStorageLayer::atomic_write(
    const std::string& model_name,
    const std::string& model_data,
    const version::ModelVersion& version) {
    SpanGuard span("ModelStorageLayer", "atomic_write", "");

    auto dir = model_dir(model_name);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return Error(ErrorCode::kWriteFailed,
                     "Failed to create model directory: " + ec.message(),
                     "ModelStorageLayer");
    }

    const auto& version_id = version.version_id;

    // Phase 1: write to .pending file
    auto pending = pending_path(model_name, version_id);
    {
        std::ofstream out(pending, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return Error(ErrorCode::kWriteFailed,
                         "Phase 1 failed: cannot open .pending file",
                         "ModelStorageLayer");
        }
        out.write(model_data.data(),
                  static_cast<std::streamsize>(model_data.size()));
        out.close();
        if (out.fail()) {
            return Error(ErrorCode::kWriteFailed,
                         "Phase 1 failed: write to .pending failed",
                         "ModelStorageLayer");
        }
    }

    // Phase 2: rename .pending to .parquet (atomic on same filesystem)
    auto final_path = checkpoint_path(model_name, version_id);
    std::filesystem::rename(pending, final_path, ec);
    if (ec) {
        return Error(ErrorCode::kWriteFailed,
                     "Phase 2 failed: rename .pending to .parquet: " +
                         ec.message(),
                     "ModelStorageLayer");
    }

    // Phase 3: audit span marker (SpanGuard already active)
    span.set_attribute("phase3", "audit_marker");

    // Update cache
    update_cache(cache_key(model_name, version_id), model_data);

    scaffold::MetricsRegistry::instance()
        .counter("model_storage.atomic_write")
        .increment();
    scaffold::MetricsRegistry::instance()
        .histogram("model_storage.atomic_write_bytes")
        .observe(static_cast<double>(model_data.size()));
    return {};
}

Result<void> ModelStorageLayer::recover_interrupted() {
    SpanGuard span("ModelStorageLayer", "recover_interrupted", "");

    auto models_dir = root_ / "models";
    if (!std::filesystem::exists(models_dir)) {
        return {};
    }

    std::error_code ec;
    int cleaned = 0;
    for (const auto& model_entry :
         std::filesystem::directory_iterator(models_dir, ec)) {
        if (!model_entry.is_directory()) continue;

        for (const auto& file_entry :
             std::filesystem::directory_iterator(model_entry.path(), ec)) {
            if (file_entry.is_regular_file() &&
                file_entry.path().extension() == ".pending") {
                std::filesystem::remove(file_entry.path(), ec);
                if (ec) {
                    return Error(ErrorCode::kWriteFailed,
                                 "Failed to remove orphan .pending file: " +
                                     file_entry.path().string(),
                                 "ModelStorageLayer");
                }
                ++cleaned;
            }
        }
    }

    span.set_attribute("cleaned_count", std::to_string(cleaned));

    scaffold::MetricsRegistry::instance()
        .counter("model_storage.recover_interrupted")
        .increment();
    scaffold::MetricsRegistry::instance()
        .gauge("model_storage.recover_cleaned")
        .set(static_cast<double>(cleaned));
    return {};
}

}  // namespace synthgen::storage::model
