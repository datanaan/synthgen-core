#pragma once

#include "storage/backend.h"
#include "storage/metadata.h"
#include "storage/parquet_io.h"

#include <filesystem>

namespace synthgen::storage {

class ObjectStoreBackend : public StorageBackend {
public:
    explicit ObjectStoreBackend(const std::filesystem::path& data_root);

    Result<void> register_table(const std::string& table_id,
                                 const std::string& schema_json) override;
    Result<bool> has_table(const std::string& table_id) const override;

    Result<std::string> append(const std::string& table_id,
                                std::shared_ptr<arrow::Table> data) override;

    Result<std::shared_ptr<arrow::Table>> scan(
        const std::string& table_id,
        const std::vector<std::string>& columns = {},
        const std::optional<ScanPredicate>& pred = std::nullopt) override;

    Result<std::vector<VersionMeta>> list_versions(
        const std::string& table_id) const override;

private:
    std::filesystem::path data_root_;
    MetadataManager metadata_;
    ParquetReader reader_;
    ParquetWriter writer_;

    std::filesystem::path table_path(const std::string& table_id) const;
    std::filesystem::path base_path(const std::string& table_id) const;
};

}  // namespace synthgen::storage
