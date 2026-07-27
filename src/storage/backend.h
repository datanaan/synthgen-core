#pragma once

#include "common/result.h"
#include "common/types.h"
#include "storage/metadata.h"

#include <arrow/table.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace synthgen::storage {

class ArrowTableIterator {
public:
    virtual ~ArrowTableIterator() = default;
    virtual Result<std::shared_ptr<arrow::Table>> next_batch() = 0;
};

struct ScanPredicate {
    std::string column;
    double min_value;
    double max_value;
};

class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    virtual Result<void> register_table(const std::string& table_id,
                                         const std::string& schema_json) = 0;
    virtual Result<bool> has_table(const std::string& table_id) const = 0;

    virtual Result<std::string> append(const std::string& table_id,
                                        std::shared_ptr<arrow::Table> data) = 0;

    virtual Result<std::shared_ptr<arrow::Table>> scan(
        const std::string& table_id,
        const std::vector<std::string>& columns = {},
        const std::optional<ScanPredicate>& pred = std::nullopt) = 0;

    virtual Result<std::vector<VersionMeta>> list_versions(
        const std::string& table_id) const = 0;
};

}  // namespace synthgen::storage
