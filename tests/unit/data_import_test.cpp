#include <gtest/gtest.h>
#include "storage/data_importer.h"
#include "storage/object_store_backend.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "storage/parquet_io.h"

#include <arrow/api.h>
#include <arrow/builder.h>
#include <filesystem>

using namespace synthgen;
using namespace synthgen::storage;

namespace {

schema::Schema make_import_schema() {
    schema::Schema s;
    s.type_name = "sensor";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    ColumnDef press;
    press.name = "pressure";
    press.type = DataType::kFloat;
    press.range_min = 900.0;
    press.range_max = 1100.0;
    s.columns.push_back(press);
    return s;
}

std::shared_ptr<arrow::Table> make_parquet_table(int rows) {
    arrow::DoubleBuilder temp_builder, press_builder;
    for (int i = 0; i < rows; i++) {
        // Keep within schema range: temp [-50, 80], press [900, 1100]
        double t = -50.0 + (130.0 * i / std::max(rows - 1, 1));
        double p = 900.0 + (200.0 * i / std::max(rows - 1, 1));
        temp_builder.Append(t);
        press_builder.Append(p);
    }
    std::shared_ptr<arrow::Array> temp_arr, press_arr;
    temp_builder.Finish(&temp_arr);
    press_builder.Finish(&press_arr);
    auto schema = arrow::schema({
        arrow::field("temperature", arrow::float64()),
        arrow::field("pressure", arrow::float64()),
    });
    return arrow::Table::Make(schema, {temp_arr, press_arr});
}

std::string write_test_parquet(const std::string& dir, int rows) {
    auto table = make_parquet_table(rows);
    std::string path = dir + "/test_data.parquet";
    ParquetWriter writer;
    auto result = writer.write(path, table);
    EXPECT_TRUE(result.ok()) << result.error().message;
    return path;
}

}  // namespace

class DataImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / ("import_test_" + std::to_string(::getpid()));
        std::filesystem::create_directories(test_dir_);
    }
    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
    std::filesystem::path test_dir_;
};

TEST_F(DataImportTest, BasicImport) {
    auto path = write_test_parquet(test_dir_, 100);
    auto schema = make_import_schema();

    ObjectStoreBackend backend(test_dir_ / "storage");
    DataImporter importer(backend);
    auto result = importer.import(schema, path);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_imported, 100);
}

TEST_F(DataImportTest, EmptyFile) {
    auto path = write_test_parquet(test_dir_, 0);
    auto schema = make_import_schema();

    ObjectStoreBackend backend(test_dir_ / "storage2");
    DataImporter importer(backend);
    auto result = importer.import(schema, path);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_imported, 0);
}

TEST_F(DataImportTest, FileNotFound) {
    ObjectStoreBackend backend(test_dir_ / "storage3");
    DataImporter importer(backend);
    auto schema = make_import_schema();
    auto result = importer.import(schema, "/nonexistent/path.parquet");
    EXPECT_FALSE(result.ok());
}

TEST_F(DataImportTest, CompatibilityCheck) {
    auto path = write_test_parquet(test_dir_, 50);
    auto schema = make_import_schema();

    ObjectStoreBackend backend(test_dir_ / "storage4");
    DataImporter importer(backend);
    auto report = importer.check_compatibility(schema, path);
    ASSERT_TRUE(report.ok()) << report.error().message;
    EXPECT_TRUE(report.value().compatible);
    EXPECT_EQ(report.value().parquet_row_count, 50);
    EXPECT_TRUE(report.value().missing_columns.empty());
}

TEST_F(DataImportTest, CompatibilityMissingColumn) {
    auto path = write_test_parquet(test_dir_, 10);
    schema::Schema s;
    s.type_name = "bad";
    ColumnDef c;
    c.name = "nonexistent";
    c.type = DataType::kFloat;
    s.columns.push_back(c);

    ObjectStoreBackend backend(test_dir_ / "storage5");
    DataImporter importer(backend);
    auto report = importer.check_compatibility(s, path);
    ASSERT_TRUE(report.ok());
    EXPECT_FALSE(report.value().compatible);
    EXPECT_EQ(report.value().missing_columns.size(), 1u);
    EXPECT_EQ(report.value().missing_columns[0], "nonexistent");
}

TEST_F(DataImportTest, StrictModeRejectsOutOfRange) {
    // Create table with out-of-range values
    arrow::DoubleBuilder temp_builder, press_builder;
    for (int i = 0; i < 50; i++) {
        temp_builder.Append(200.0);  // exceeds max 80.0
        press_builder.Append(1013.0);
    }
    std::shared_ptr<arrow::Array> temp_arr, press_arr;
    temp_builder.Finish(&temp_arr);
    press_builder.Finish(&press_arr);
    auto table = arrow::Table::Make(
        arrow::schema({arrow::field("temperature", arrow::float64()),
                       arrow::field("pressure", arrow::float64())}),
        {temp_arr, press_arr});

    std::string path = (test_dir_ / "bad_data.parquet").string();
    ParquetWriter writer;
    writer.write(path, table);

    auto schema = make_import_schema();
    ObjectStoreBackend backend(test_dir_ / "storage6");
    DataImporter importer(backend);
    auto result = importer.import(schema, path, ImportMode::kStrict);
    EXPECT_FALSE(result.ok());
}

TEST_F(DataImportTest, OneRowImport) {
    auto path = write_test_parquet(test_dir_, 1);
    auto schema = make_import_schema();

    ObjectStoreBackend backend(test_dir_ / "storage7");
    DataImporter importer(backend);
    auto result = importer.import(schema, path);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_imported, 1);
}

TEST_F(DataImportTest, LargeImport) {
    auto path = write_test_parquet(test_dir_, 5000);
    auto schema = make_import_schema();

    ObjectStoreBackend backend(test_dir_ / "storage8");
    DataImporter importer(backend);
    auto result = importer.import(schema, path);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_imported, 5000);
}

TEST_F(DataImportTest, LoadDataExecutor) {
    auto path = write_test_parquet(test_dir_, 100);
    auto schema = make_import_schema();

    schema::SchemaRegistry registry;
    registry.register_schema(schema);

    ObjectStoreBackend backend(test_dir_ / "storage9");
    LoadDataExecutor executor;

    parser::ast::LoadDataStmt stmt;
    stmt.type_name = "sensor";
    stmt.file_path = path;
    auto result = executor.execute(stmt, registry, backend);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().rows_imported, 100);
}

TEST_F(DataImportTest, LoadDataUndefinedType) {
    schema::SchemaRegistry registry;
    ObjectStoreBackend backend(test_dir_ / "storage10");
    LoadDataExecutor executor;

    parser::ast::LoadDataStmt stmt;
    stmt.type_name = "nonexistent";
    stmt.file_path = "/some/file.parquet";
    auto result = executor.execute(stmt, registry, backend);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kUndefinedType);
}
