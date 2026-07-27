#pragma once
#include "common/result.h"
#include "common/types.h"
#include "storage/backend.h"
#include "storage/parquet_io.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "parser/ast.h"
#include <arrow/table.h>
#include <string>
#include <vector>

namespace synthgen::storage {

struct ImportError {
    int64_t row_index;
    std::string column;
    std::string reason;
};

struct CompatibilityReport {
    bool compatible = true;
    std::vector<std::string> missing_columns;
    std::vector<std::string> extra_columns;
    std::vector<std::string> type_mismatches;
    std::vector<std::string> range_violations;
    int64_t parquet_row_count = 0;
    int64_t parquet_column_count = 0;
};

enum class ImportMode {
    kStrict,   // any mismatch -> fail
    kLenient,  // skip mismatched rows
};

struct ImportResult {
    int64_t rows_imported = 0;
    int64_t rows_skipped = 0;
    std::string table_id;
    std::vector<ImportError> errors;  // max 100
};

class DataImporter {
public:
    explicit DataImporter(StorageBackend& storage);

    Result<ImportResult> import(const synthgen::schema::Schema& schema,
                                const std::string& parquet_path,
                                ImportMode mode = ImportMode::kStrict);

    Result<CompatibilityReport> check_compatibility(
        const synthgen::schema::Schema& schema,
        const std::string& parquet_path);

private:
    StorageBackend& storage_;
    ParquetReader reader_;
};

class LoadDataExecutor {
public:
    Result<ImportResult> execute(const synthgen::parser::ast::LoadDataStmt& stmt,
                                  synthgen::schema::SchemaRegistry& registry,
                                  StorageBackend& storage);
};

}  // namespace synthgen::storage
