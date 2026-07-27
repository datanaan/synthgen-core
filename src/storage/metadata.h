#pragma once

#include "common/result.h"
#include "common/types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace synthgen::storage {

struct VersionMeta {
    std::string version_id;
    std::string table_id;
    Timestamp created_at;
    int64_t row_count;
    std::string schema_hash;
};

struct SnapshotRef {
    std::string snapshot_id;
    int64_t row_count;
    Timestamp created_at;
};

struct TableMetadata {
    std::string table_id;
    std::string schema_json;
    std::string schema_hash;
    Timestamp created_at;
    std::vector<VersionMeta> versions;
    std::vector<SnapshotRef> snapshots;
};

class MetadataManager {
public:
    explicit MetadataManager(const std::filesystem::path& table_dir);

    Result<void> create_table(const std::string& table_id,
                               const std::string& schema_json);
    Result<const TableMetadata*> get_table(const std::string& table_id) const;
    Result<void> add_version(const std::string& table_id, VersionMeta version);
    Result<void> add_snapshot(const std::string& table_id, SnapshotRef snapshot);
    int64_t next_part_number();

    Result<void> flush();    // atomic write to metadata.json
    Result<void> reload();   // reload from metadata.json

private:
    std::filesystem::path table_dir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TableMetadata> tables_;
    int64_t next_part_ = 1;
};

}  // namespace synthgen::storage
