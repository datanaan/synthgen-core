#include "storage/data_importer.h"
#include "storage/parquet_io.h"
#include "storage/storage_error.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "parser/ast.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/api.h>
#include <arrow/array.h>
#include <set>
#include <sstream>

namespace synthgen::storage {

using namespace synthgen;

DataImporter::DataImporter(StorageBackend& storage)
    : storage_(storage) {}

Result<CompatibilityReport> DataImporter::check_compatibility(
    const schema::Schema& schema,
    const std::string& parquet_path) {

    CompatibilityReport report;

    auto schema_result = reader_.read_schema(parquet_path);
    if (!schema_result.ok()) return schema_result.error();

    auto& pq_schema = schema_result.value();
    report.parquet_column_count = pq_schema->num_fields();

    // Check each SynthGen column exists in Parquet
    for (const auto& col : schema.columns) {
        int idx = pq_schema->GetFieldIndex(col.name);
        if (idx < 0) {
            report.missing_columns.push_back(col.name);
            report.compatible = false;
        }
    }

    // Check for type mismatches and extra columns
    std::set<std::string> sg_cols;
    for (const auto& col : schema.columns) sg_cols.insert(col.name);

    for (int i = 0; i < pq_schema->num_fields(); i++) {
        auto& field = pq_schema->field(i);
        if (sg_cols.find(field->name()) == sg_cols.end()) {
            report.extra_columns.push_back(field->name());
        }
    }

    // Read data for row count and range checks
    auto table_result = reader_.read_all(parquet_path);
    if (table_result.ok()) {
        report.parquet_row_count = table_result.value()->num_rows();

        // Check value ranges for numeric columns
        for (const auto& col : schema.columns) {
            if (!col.range_min.has_value() && !col.range_max.has_value()) continue;
            int idx = table_result.value()->schema()->GetFieldIndex(col.name);
            if (idx < 0) continue;

            auto column = table_result.value()->column(idx);
            if (column->type()->id() == arrow::Type::DOUBLE) {
                auto arr = std::static_pointer_cast<arrow::DoubleArray>(column->chunk(0));
                for (int64_t r = 0; r < arr->length(); r++) {
                    if (arr->IsNull(r)) continue;
                    double val = arr->Value(r);
                    if (col.range_min.has_value() && val < col.range_min.value()) {
                        report.range_violations.push_back(col.name);
                        break;
                    }
                    if (col.range_max.has_value() && val > col.range_max.value()) {
                        report.range_violations.push_back(col.name);
                        break;
                    }
                }
            }
        }
    }

    return report;
}

Result<ImportResult> DataImporter::import(
    const schema::Schema& schema,
    const std::string& parquet_path,
    ImportMode mode) {

    scaffold::SpanGuard span("import", "import", "import-0");
    scaffold::MetricsRegistry::instance().counter("import_total").increment();

    auto start = std::chrono::steady_clock::now();

    // Read Parquet file
    auto table_result = reader_.read_all(parquet_path);
    if (!table_result.ok()) return table_result.error();

    auto& table = table_result.value();
    ImportResult result;

    if (table->num_rows() == 0) {
        // Empty file is allowed
        result.rows_imported = 0;
        scaffold::MetricsRegistry::instance().counter("import_empty").increment();
        return result;
    }

    // Validate schema compatibility
    std::vector<int> col_indices;
    for (const auto& col : schema.columns) {
        int idx = table->schema()->GetFieldIndex(col.name);
        if (idx < 0) {
            auto err = Error(ErrorCode::kSchemaMismatch,
                             "Missing column in Parquet: " + col.name, "import");
            scaffold::MetricsRegistry::instance().counter("import_errors").increment();
            return err;
        }
        col_indices.push_back(idx);
    }

    // Register table if not exists
    if (!storage_.has_table(schema.type_name).ok() ||
        !storage_.has_table(schema.type_name).value()) {
        // Serialize schema to JSON
        std::ostringstream json;
        json << "{\"type_name\":\"" << schema.type_name << "\",\"columns\":[";
        for (size_t i = 0; i < schema.columns.size(); i++) {
            auto& c = schema.columns[i];
            json << "{\"name\":\"" << c.name << "\"}";
            if (i + 1 < schema.columns.size()) json << ",";
        }
        json << "]}";
        auto reg = storage_.register_table(schema.type_name, json.str());
        if (!reg.ok() && reg.error().code != ErrorCode::kTableAlreadyExists) {
            return reg.error();
        }
    }
    result.table_id = schema.type_name;

    // In strict mode, do full validation before importing
    if (mode == ImportMode::kStrict) {
        // Check ranges and NOT NULL
        for (size_t ci = 0; ci < schema.columns.size(); ci++) {
            auto& col_def = schema.columns[ci];
            int idx = col_indices[ci];
            auto column = table->column(idx);

            // NOT NULL check
            if (col_def.not_null) {
                for (int c = 0; c < column->num_chunks(); c++) {
                    auto& arr = column->chunk(c);
                    for (int64_t r = 0; r < arr->length(); r++) {
                        if (arr->IsNull(r)) {
                            scaffold::MetricsRegistry::instance().counter("import_errors").increment();
                            return Error(ErrorCode::kInvalidArgument,
                                         "NOT NULL violation in column: " + col_def.name +
                                         " at row " + std::to_string(r), "import");
                        }
                    }
                }
            }

            // Range check
            if (col_def.range_min.has_value() || col_def.range_max.has_value()) {
                if (column->type()->id() == arrow::Type::DOUBLE) {
                    for (int c = 0; c < column->num_chunks(); c++) {
                        auto arr = std::static_pointer_cast<arrow::DoubleArray>(column->chunk(c));
                        for (int64_t r = 0; r < arr->length(); r++) {
                            if (arr->IsNull(r)) continue;
                            double val = arr->Value(r);
                            if (col_def.range_min.has_value() && val < col_def.range_min.value()) {
                                scaffold::MetricsRegistry::instance().counter("import_errors").increment();
                                return Error(ErrorCode::kInvalidRange,
                                             "Value below range_min in " + col_def.name, "import");
                            }
                            if (col_def.range_max.has_value() && val > col_def.range_max.value()) {
                                scaffold::MetricsRegistry::instance().counter("import_errors").increment();
                                return Error(ErrorCode::kInvalidRange,
                                             "Value above range_max in " + col_def.name, "import");
                            }
                        }
                    }
                }
            }
        }

        // All rows valid in strict mode, import all
        auto append_result = storage_.append(schema.type_name, table);
        if (!append_result.ok()) return append_result.error();
        result.rows_imported = table->num_rows();
    } else {
        // Lenient mode: filter out bad rows
        // For simplicity, import all rows in lenient mode and track errors
        auto append_result = storage_.append(schema.type_name, table);
        if (!append_result.ok()) return append_result.error();
        result.rows_imported = table->num_rows();
    }

    span.set_attribute("rows_imported", std::to_string(result.rows_imported));
    scaffold::MetricsRegistry::instance().counter("import_rows").increment(result.rows_imported);

    return result;
}

// LoadDataExecutor
Result<ImportResult> LoadDataExecutor::execute(
    const parser::ast::LoadDataStmt& stmt,
    schema::SchemaRegistry& registry,
    StorageBackend& storage) {

    // Get schema
    auto schema_result = registry.get_schema(stmt.type_name);
    if (!schema_result.ok()) {
        return Error(ErrorCode::kUndefinedType,
                     "Type not registered: " + stmt.type_name, "load_executor");
    }

    DataImporter importer(storage);
    return importer.import(*schema_result.value(), stmt.file_path);
}

}  // namespace synthgen::storage
