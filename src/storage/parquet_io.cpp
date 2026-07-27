#include "storage/parquet_io.h"
#include "storage/storage_error.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

#include <iomanip>
#include <sstream>

namespace synthgen::storage {

// ---------------------------------------------------------------------------
// ParquetReader
// ---------------------------------------------------------------------------

Result<ArrowTable> ParquetReader::read_all(const std::string& path) {
    return read_table_internal(path, {});
}

Result<ArrowTable> ParquetReader::read_columns(const std::string& path,
                                                const std::vector<std::string>& columns) {
    if (columns.empty()) {
        return read_all(path);
    }
    return read_table_internal(path, columns);
}

Result<std::shared_ptr<arrow::Schema>> ParquetReader::read_schema(
    const std::string& path) {
    auto file_result = arrow::io::ReadableFile::Open(path);
    if (!file_result.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Cannot open file: " + path + " (" +
                                    file_result.status().ToString() + ")");
    }

    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto status = parquet::arrow::OpenFile(
        *file_result, arrow::default_memory_pool(), &reader);
    if (!status.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Cannot open parquet reader: " + path + " (" +
                                    status.ToString() + ")");
    }

    std::shared_ptr<arrow::Schema> schema;
    status = reader->GetSchema(&schema);
    if (!status.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Cannot read schema: " + path + " (" +
                                    status.ToString() + ")");
    }

    return schema;
}

Result<ArrowTable> ParquetReader::read_table_internal(
    const std::string& path, const std::vector<std::string>& columns) {
    auto file_result = arrow::io::ReadableFile::Open(path);
    if (!file_result.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Cannot open file: " + path + " (" +
                                    file_result.status().ToString() + ")");
    }

    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto status = parquet::arrow::OpenFile(
        *file_result, arrow::default_memory_pool(), &reader);
    if (!status.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Cannot open parquet reader: " + path + " (" +
                                    status.ToString() + ")");
    }

    std::shared_ptr<arrow::Table> table;
    if (columns.empty()) {
        status = reader->ReadTable(&table);
    } else {
        // Read schema first to resolve column names to indices
        std::shared_ptr<arrow::Schema> schema;
        auto schema_status = reader->GetSchema(&schema);
        if (!schema_status.ok()) {
            return MakeStorageError(ErrorCode::kReadFailed,
                                    "Cannot read schema for column projection: " +
                                        schema_status.ToString());
        }
        std::vector<int> indices;
        for (const auto& col_name : columns) {
            auto idx = schema->GetFieldIndex(col_name);
            if (idx < 0) {
                return MakeStorageError(ErrorCode::kColumnNotFound,
                                        "Column not found: " + col_name);
            }
            indices.push_back(idx);
        }
        status = reader->ReadTable(indices, &table);
    }

    if (!status.ok()) {
        return MakeStorageError(ErrorCode::kReadFailed,
                                "Failed to read table: " + path + " (" +
                                    status.ToString() + ")");
    }

    return table;
}

// ---------------------------------------------------------------------------
// ParquetWriter
// ---------------------------------------------------------------------------

Result<void> ParquetWriter::write(const std::string& path,
                                   const ArrowTable& table) {
    auto file_result = arrow::io::FileOutputStream::Open(path);
    if (!file_result.ok()) {
        return MakeStorageError(ErrorCode::kWriteFailed,
                                "Cannot open file for writing: " + path + " (" +
                                    file_result.status().ToString() + ")");
    }

    auto write_status = parquet::arrow::WriteTable(
        *table, arrow::default_memory_pool(), *file_result,
        /*chunk_size=*/65536);

    if (!write_status.ok()) {
        return MakeStorageError(ErrorCode::kWriteFailed,
                                "Failed to write parquet: " + path + " (" +
                                    write_status.ToString() + ")");
    }

    return {};
}

Result<std::string> ParquetWriter::append(const std::string& dir,
                                           int64_t part_number,
                                           const ArrowTable& table) {
    // Format: part-XXXXX.parquet (5-digit zero-padded)
    std::ostringstream oss;
    oss << dir << "/part-" << std::setw(5) << std::setfill('0')
        << part_number << ".parquet";
    std::string path = oss.str();

    auto result = write(path, table);
    if (!result.ok()) {
        return result.error();
    }
    return path;
}

}  // namespace synthgen::storage
