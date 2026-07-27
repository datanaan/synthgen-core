#include "storage/object_store_backend.h"
#include "storage/storage_error.h"

#include <arrow/api.h>
#include <arrow/compute/api.h>

#include <chrono>
#include <sstream>

namespace synthgen::storage {

namespace {

Timestamp now_micros() {
    auto now = std::chrono::system_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch());
    return micros.count();
}

int64_t total_rows(const std::shared_ptr<arrow::Table>& table) {
    return table ? table->num_rows() : 0;
}

// Concatenate all chunks of a ChunkedArray into a single Array
Result<std::shared_ptr<arrow::Array>> flatten_chunks(
    const std::shared_ptr<arrow::ChunkedArray>& chunked) {
    if (chunked->num_chunks() == 0) {
        // Return empty array of the right type
        auto result = arrow::MakeEmptyArray(chunked->type());
        if (!result.ok()) {
            return MakeStorageError(ErrorCode::kInternalError,
                                    "Failed to create empty array");
        }
        return *result;
    }
    if (chunked->num_chunks() == 1) {
        return chunked->chunk(0);
    }
    arrow::ArrayVector chunks;
    chunks.reserve(chunked->num_chunks());
    for (int i = 0; i < chunked->num_chunks(); ++i) {
        chunks.push_back(chunked->chunk(i));
    }
    auto concat_result = arrow::Concatenate(chunks);
    if (!concat_result.ok()) {
        return MakeStorageError(ErrorCode::kInternalError,
                                "Failed to concatenate chunks: " +
                                    concat_result.status().ToString());
    }
    return *concat_result;
}

// Apply ScanPredicate: filter rows where column value is in [min_value, max_value]
Result<std::shared_ptr<arrow::Table>> apply_predicate(
    std::shared_ptr<arrow::Table> table, const ScanPredicate& pred) {
    auto schema = table->schema();
    int idx = schema->GetFieldIndex(pred.column);
    if (idx < 0) {
        return MakeStorageError(ErrorCode::kColumnNotFound,
                                "Predicate column not found: " + pred.column);
    }

    auto chunked = table->column(idx);
    auto array_result = flatten_chunks(chunked);
    if (!array_result.ok()) {
        return array_result.error();
    }
    auto array = array_result.value();

    // Build a boolean filter mask
    arrow::BooleanBuilder builder;
    double min_v = pred.min_value;
    double max_v = pred.max_value;

    // Helper to safely append and check status
    auto safe_append = [&](bool val) -> arrow::Status {
        return builder.Append(val);
    };

    if (array->type_id() == arrow::Type::DOUBLE) {
        auto typed = std::static_pointer_cast<arrow::DoubleArray>(array);
        for (int64_t i = 0; i < typed->length(); ++i) {
            if (typed->IsNull(i)) {
                if (!safe_append(false).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            } else {
                double val = typed->Value(i);
                if (!safe_append(val >= min_v && val <= max_v).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            }
        }
    } else if (array->type_id() == arrow::Type::FLOAT) {
        auto typed = std::static_pointer_cast<arrow::FloatArray>(array);
        for (int64_t i = 0; i < typed->length(); ++i) {
            if (typed->IsNull(i)) {
                if (!safe_append(false).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            } else {
                float val = typed->Value(i);
                if (!safe_append(val >= min_v && val <= max_v).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            }
        }
    } else if (array->type_id() == arrow::Type::INT64) {
        auto typed = std::static_pointer_cast<arrow::Int64Array>(array);
        for (int64_t i = 0; i < typed->length(); ++i) {
            if (typed->IsNull(i)) {
                if (!safe_append(false).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            } else {
                int64_t val = typed->Value(i);
                if (!safe_append(static_cast<double>(val) >= min_v &&
                               static_cast<double>(val) <= max_v).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            }
        }
    } else if (array->type_id() == arrow::Type::INT32) {
        auto typed = std::static_pointer_cast<arrow::Int32Array>(array);
        for (int64_t i = 0; i < typed->length(); ++i) {
            if (typed->IsNull(i)) {
                if (!safe_append(false).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            } else {
                int32_t val = typed->Value(i);
                if (!safe_append(static_cast<double>(val) >= min_v &&
                               static_cast<double>(val) <= max_v).ok()) {
                    return MakeStorageError(ErrorCode::kInternalError,
                                            "Builder append failed");
                }
            }
        }
    } else {
        // Unsupported type for predicate — return table unfiltered
        return table;
    }

    std::shared_ptr<arrow::BooleanArray> filter;
    auto status = builder.Finish(&filter);
    if (!status.ok()) {
        return MakeStorageError(ErrorCode::kInternalError,
                                "Failed to build filter array");
    }

    auto result = arrow::compute::Filter(table, filter);
    if (!result.ok()) {
        return MakeStorageError(ErrorCode::kInternalError,
                                "Filter operation failed: " +
                                    result.status().ToString());
    }

    return result.ValueOrDie().table();
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// ObjectStoreBackend
// ---------------------------------------------------------------------------

ObjectStoreBackend::ObjectStoreBackend(const std::filesystem::path& data_root)
    : data_root_(data_root),
      metadata_(data_root / "tables") {
    std::error_code ec;
    std::filesystem::create_directories(data_root_, ec);
    // Try to reload existing metadata
    auto reload_result = metadata_.reload();
    // Ignore reload errors — fresh backend is fine
    (void)reload_result;
}

std::filesystem::path ObjectStoreBackend::table_path(
    const std::string& table_id) const {
    return data_root_ / "tables" / table_id;
}

std::filesystem::path ObjectStoreBackend::base_path(
    const std::string& table_id) const {
    return table_path(table_id) / "base";
}

Result<void> ObjectStoreBackend::register_table(
    const std::string& table_id, const std::string& schema_json) {
    auto result = metadata_.create_table(table_id, schema_json);
    if (!result.ok()) {
        return result;
    }

    // Create base directory
    auto bp = base_path(table_id);
    std::error_code ec;
    std::filesystem::create_directories(bp, ec);
    if (ec) {
        return MakeStorageError(ErrorCode::kWriteFailed,
                                "Cannot create base directory: " +
                                    bp.string() + " (" + ec.message() + ")");
    }

    return metadata_.flush();
}

Result<bool> ObjectStoreBackend::has_table(const std::string& table_id) const {
    auto result = metadata_.get_table(table_id);
    if (result.ok()) return true;
    if (result.error().code == ErrorCode::kTableNotFound) return false;
    return result.error();
}

Result<std::string> ObjectStoreBackend::append(
    const std::string& table_id, std::shared_ptr<arrow::Table> data) {
    auto table_result = metadata_.get_table(table_id);
    if (!table_result.ok()) {
        return table_result.error();
    }

    int64_t part_num = metadata_.next_part_number();
    auto bp = base_path(table_id);
    auto path_result = writer_.append(bp.string(), part_num, data);
    if (!path_result.ok()) {
        return path_result.error();
    }

    // Record version
    VersionMeta version;
    version.version_id = "v_" + std::to_string(part_num);
    version.table_id = table_id;
    version.created_at = now_micros();
    version.row_count = total_rows(data);
    version.schema_hash = table_result.value()->schema_hash;

    auto add_result = metadata_.add_version(table_id, std::move(version));
    if (!add_result.ok()) {
        return add_result.error();
    }

    auto flush_result = metadata_.flush();
    if (!flush_result.ok()) {
        return flush_result.error();
    }

    return path_result;
}

Result<std::shared_ptr<arrow::Table>> ObjectStoreBackend::scan(
    const std::string& table_id,
    const std::vector<std::string>& columns,
    const std::optional<ScanPredicate>& pred) {
    auto table_result = metadata_.get_table(table_id);
    if (!table_result.ok()) {
        return table_result.error();
    }

    auto bp = base_path(table_id);
    std::error_code ec;
    if (!std::filesystem::exists(bp, ec) || ec) {
        // No data yet — return empty table
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        auto empty_schema = std::make_shared<arrow::Schema>(fields);
        return arrow::Table::Make(empty_schema, arrays, 0);
    }

    // Read all parquet files in the base directory
    std::vector<std::shared_ptr<arrow::Table>> tables;
    for (const auto& entry :
         std::filesystem::directory_iterator(bp, ec)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path().string();
        if (path.size() < 8 ||
            path.substr(path.size() - 8) != ".parquet") {
            continue;
        }

        auto read_result = columns.empty()
            ? reader_.read_all(path)
            : reader_.read_columns(path, columns);
        if (!read_result.ok()) {
            return read_result;
        }
        tables.push_back(read_result.value());
    }

    if (tables.empty()) {
        // No parquet files found
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        auto empty_schema = std::make_shared<arrow::Schema>(fields);
        return arrow::Table::Make(empty_schema, arrays, 0);
    }

    // Concatenate all tables
    std::shared_ptr<arrow::Table> combined;
    if (tables.size() == 1) {
        combined = tables[0];
    } else {
        auto concat_result = arrow::ConcatenateTables(tables);
        if (!concat_result.ok()) {
            return MakeStorageError(
                ErrorCode::kInternalError,
                "Failed to concatenate tables: " +
                    concat_result.status().ToString());
        }
        combined = *concat_result;
    }

    // Apply predicate if provided
    if (pred.has_value()) {
        return apply_predicate(combined, pred.value());
    }

    return combined;
}

Result<std::vector<VersionMeta>> ObjectStoreBackend::list_versions(
    const std::string& table_id) const {
    auto table_result = metadata_.get_table(table_id);
    if (!table_result.ok()) {
        return table_result.error();
    }
    return table_result.value()->versions;
}

}  // namespace synthgen::storage
