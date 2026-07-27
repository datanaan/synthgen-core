#include <gtest/gtest.h>

#include "storage/parquet_io.h"
#include "storage/storage_error.h"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / "synthgen_parquet_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::shared_ptr<arrow::Table> make_simple_table(int64_t rows = 10) {
    arrow::Int64Builder id_builder;
    arrow::DoubleBuilder val_builder;
    for (int64_t i = 0; i < rows; ++i) {
        id_builder.Append(i);
        val_builder.Append(static_cast<double>(i) * 1.5);
    }
    std::shared_ptr<arrow::Array> id_array;
    std::shared_ptr<arrow::Array> val_array;
    id_builder.Finish(&id_array);
    val_builder.Finish(&val_array);
    auto schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("value", arrow::float64())});
    return arrow::Table::Make(schema, {id_array, val_array});
}

std::shared_ptr<arrow::Table> make_string_table(int64_t rows = 5) {
    arrow::Int64Builder id_builder;
    arrow::StringBuilder name_builder;
    for (int64_t i = 0; i < rows; ++i) {
        id_builder.Append(i);
        name_builder.Append("name_" + std::to_string(i));
    }
    std::shared_ptr<arrow::Array> id_array;
    std::shared_ptr<arrow::Array> name_array;
    id_builder.Finish(&id_array);
    name_builder.Finish(&name_array);
    auto schema = arrow::schema(
        {arrow::field("id", arrow::int64()),
         arrow::field("name", arrow::utf8())});
    return arrow::Table::Make(schema, {id_array, name_array});
}

}  // namespace

// Test 1: Write and read back all columns
TEST(ParquetIOTest, WriteAndReadAllColumns) {
    auto dir = make_temp_dir();
    auto table = make_simple_table(10);
    std::string path = (dir / "test.parquet").string();

    synthgen::storage::ParquetWriter writer;
    auto write_result = writer.write(path, table);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    synthgen::storage::ParquetReader reader;
    auto read_result = reader.read_all(path);
    ASSERT_TRUE(read_result.ok()) << read_result.error().message;

    auto read_table = read_result.value();
    ASSERT_EQ(read_table->num_rows(), 10);
    ASSERT_EQ(read_table->num_columns(), 2);

    auto id_col = std::static_pointer_cast<arrow::Int64Array>(
        read_table->column(0)->chunk(0));
    EXPECT_EQ(id_col->Value(0), 0);
    EXPECT_EQ(id_col->Value(9), 9);

    auto val_col = std::static_pointer_cast<arrow::DoubleArray>(
        read_table->column(1)->chunk(0));
    EXPECT_DOUBLE_EQ(val_col->Value(0), 0.0);
    EXPECT_DOUBLE_EQ(val_col->Value(5), 7.5);
}

// Test 2: Write and read back specific columns
TEST(ParquetIOTest, ReadSpecificColumns) {
    auto dir = make_temp_dir();
    auto table = make_simple_table(10);
    std::string path = (dir / "test_cols.parquet").string();

    synthgen::storage::ParquetWriter writer;
    writer.write(path, table);

    synthgen::storage::ParquetReader reader;
    auto result = reader.read_columns(path, {"value"});
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto read_table = result.value();
    ASSERT_EQ(read_table->num_columns(), 1);
    EXPECT_EQ(read_table->num_rows(), 10);
    // The single column should be "value"
    EXPECT_EQ(read_table->schema()->field(0)->name(), "value");
}

// Test 3: Read schema only (no data loaded)
TEST(ParquetIOTest, ReadSchemaOnly) {
    auto dir = make_temp_dir();
    auto table = make_simple_table(5);
    std::string path = (dir / "schema_test.parquet").string();

    synthgen::storage::ParquetWriter writer;
    writer.write(path, table);

    synthgen::storage::ParquetReader reader;
    auto result = reader.read_schema(path);
    ASSERT_TRUE(result.ok()) << result.error().message;

    auto schema = result.value();
    ASSERT_EQ(schema->num_fields(), 2);
    EXPECT_EQ(schema->field(0)->name(), "id");
    EXPECT_EQ(schema->field(1)->name(), "value");
    EXPECT_EQ(schema->field(0)->type()->id(), arrow::Type::INT64);
    EXPECT_EQ(schema->field(1)->type()->id(), arrow::Type::DOUBLE);
}

// Test 4: Append multiple parts
TEST(ParquetIOTest, AppendMultipleParts) {
    auto dir = make_temp_dir();
    auto parts_dir = dir / "parts";
    std::filesystem::create_directories(parts_dir);

    synthgen::storage::ParquetWriter writer;
    synthgen::storage::ParquetReader reader;

    for (int i = 1; i <= 3; ++i) {
        auto table = make_simple_table(5);
        auto result = writer.append(parts_dir.string(), i, table);
        ASSERT_TRUE(result.ok()) << result.error().message;
        EXPECT_FALSE(result.value().empty());
    }

    // Verify 3 part files exist
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(parts_dir)) {
        if (entry.path().string().size() >= 8 &&
            entry.path().string().substr(entry.path().string().size() - 8) == ".parquet") {
            ++count;
        }
    }
    EXPECT_EQ(count, 3);

    // Read and verify each
    auto t1 = reader.read_all((parts_dir / "part-00001.parquet").string());
    ASSERT_TRUE(t1.ok());
    EXPECT_EQ(t1.value()->num_rows(), 5);
}

// Test 5: Empty table write/read
TEST(ParquetIOTest, EmptyTable) {
    auto dir = make_temp_dir();
    auto schema = arrow::schema({arrow::field("x", arrow::int64())});
    // Create an empty array with the correct type
    arrow::Int64Builder builder;
    std::shared_ptr<arrow::Array> empty_array;
    builder.Finish(&empty_array);
    // Resize to 0 length by creating a new empty array
    auto empty_array_typed = arrow::MakeEmptyArray(arrow::int64());
    ASSERT_TRUE(empty_array_typed.ok());
    auto empty_table = arrow::Table::Make(schema, {*empty_array_typed}, 0);
    std::string path = (dir / "empty.parquet").string();

    synthgen::storage::ParquetWriter writer;
    auto write_result = writer.write(path, empty_table);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    synthgen::storage::ParquetReader reader;
    auto result = reader.read_all(path);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value()->num_rows(), 0);
    EXPECT_EQ(result.value()->num_columns(), 1);
}

// Test 6: Non-existent file returns error
TEST(ParquetIOTest, NonExistentFile) {
    synthgen::storage::ParquetReader reader;
    auto result = reader.read_all("/nonexistent/path/data.parquet");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kReadFailed);
}

// Test 7: Column not found returns error
TEST(ParquetIOTest, ColumnNotFound) {
    auto dir = make_temp_dir();
    auto table = make_simple_table(5);
    std::string path = (dir / "col_test.parquet").string();

    synthgen::storage::ParquetWriter writer;
    writer.write(path, table);

    synthgen::storage::ParquetReader reader;
    auto result = reader.read_columns(path, {"nonexistent_column"});
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kColumnNotFound);
}

// Test 8: Write to invalid path returns error
TEST(ParquetIOTest, WriteToInvalidPath) {
    auto table = make_simple_table(5);
    synthgen::storage::ParquetWriter writer;
    auto result = writer.write("/nonexistent/dir/test.parquet", table);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kWriteFailed);
}

// Test 9: Read schema of non-existent file
TEST(ParquetIOTest, ReadSchemaNonExistent) {
    synthgen::storage::ParquetReader reader;
    auto result = reader.read_schema("/nonexistent/schema.parquet");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, synthgen::ErrorCode::kReadFailed);
}

// Test 10: String column roundtrip
TEST(ParquetIOTest, StringColumnRoundtrip) {
    auto dir = make_temp_dir();
    auto table = make_string_table(5);
    std::string path = (dir / "strings.parquet").string();

    synthgen::storage::ParquetWriter writer;
    writer.write(path, table);

    synthgen::storage::ParquetReader reader;
    auto result = reader.read_all(path);
    ASSERT_TRUE(result.ok());

    auto read_table = result.value();
    EXPECT_EQ(read_table->num_rows(), 5);
    auto name_col = std::static_pointer_cast<arrow::StringArray>(
        read_table->column(1)->chunk(0));
    EXPECT_EQ(name_col->GetString(0), "name_0");
    EXPECT_EQ(name_col->GetString(4), "name_4");
}
