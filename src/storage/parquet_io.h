#pragma once

#include "common/result.h"

#include <arrow/record_batch.h>
#include <arrow/table.h>

#include <memory>
#include <string>
#include <vector>

namespace synthgen::storage {

using ArrowBatch = std::shared_ptr<arrow::RecordBatch>;
using ArrowTable = std::shared_ptr<arrow::Table>;

class ParquetReader {
public:
    Result<ArrowTable> read_all(const std::string& path);
    Result<ArrowTable> read_columns(const std::string& path,
                                     const std::vector<std::string>& columns);
    Result<std::shared_ptr<arrow::Schema>> read_schema(const std::string& path);

private:
    Result<ArrowTable> read_table_internal(const std::string& path,
                                            const std::vector<std::string>& columns);
};

class ParquetWriter {
public:
    Result<void> write(const std::string& path, const ArrowTable& table);
    Result<std::string> append(const std::string& dir, int64_t part_number,
                                const ArrowTable& table);
};

}  // namespace synthgen::storage
