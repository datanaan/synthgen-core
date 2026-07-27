// E2E Smoke Tests — 5 end-to-end scenarios validating the full SynthGen pipeline
#include <gtest/gtest.h>

#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/ast.h"
#include "schema/schema.h"
#include "schema/schema_builder.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/evidence_package.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_json.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "common/result.h"
#include "common/types.h"
#include "common/hash.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/type.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace synthgen;
using namespace synthgen::parser;
using namespace synthgen::schema;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;

// ============================================================================
// Smoke 1: SynthLang Lexer → Parser → SchemaBuilder → validate
// ============================================================================
TEST(SmokeTest, LexerParserSchemaBuilder) {
    const std::string source =
        "DEFINE TYPE sensor_log {"
        "  timestamp: DATETIME NOT NULL ORDER,"
        "  wind_speed: FLOAT [0.0, 50.0],"
        "  temperature: FLOAT [-50.0, 80.0],"
        "  status: ENUM('normal', 'warning', 'fault')"
        "};";

    // Step 1: Lex
    Lexer lexer(source);
    auto tokens_result = lexer.tokenize();
    ASSERT_TRUE(tokens_result.ok()) << "Lexer failed: " << tokens_result.error().message;
    auto& tokens = tokens_result.value();
    EXPECT_GT(tokens.size(), 0u);

    // Step 2: Parse
    Parser parser;
    auto parse_result = parser.parse(source);
    ASSERT_TRUE(parse_result.ok()) << "Parser failed: " << parse_result.error().message;
    auto& program = parse_result.value().program;
    ASSERT_EQ(program.statements.size(), 1u);

    auto* stmt = std::get_if<ast::DefineTypeStmt>(&program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name, "sensor_log");
    EXPECT_EQ(stmt->columns.size(), 4u);

    // Verify column definitions
    EXPECT_EQ(stmt->columns[0].name, "timestamp");
    EXPECT_EQ(stmt->columns[0].type, DataType::kDatetime);
    EXPECT_TRUE(stmt->columns[0].not_null);
    EXPECT_TRUE(stmt->columns[0].is_order);

    EXPECT_EQ(stmt->columns[1].name, "wind_speed");
    EXPECT_EQ(stmt->columns[1].type, DataType::kFloat);
    ASSERT_TRUE(stmt->columns[1].range_min.has_value());
    ASSERT_TRUE(stmt->columns[1].range_max.has_value());
    EXPECT_DOUBLE_EQ(stmt->columns[1].range_min.value(), 0.0);
    EXPECT_DOUBLE_EQ(stmt->columns[1].range_max.value(), 50.0);

    EXPECT_EQ(stmt->columns[3].name, "status");
    EXPECT_EQ(stmt->columns[3].type, DataType::kEnum);
    EXPECT_EQ(stmt->columns[3].enum_values.size(), 3u);

    // Step 3: SchemaBuilder
    SchemaBuilder builder;
    auto schema_result = builder.build(*stmt);
    ASSERT_TRUE(schema_result.ok()) << "SchemaBuilder failed: " << schema_result.error().message;
    auto& schema = schema_result.value();
    EXPECT_EQ(schema.type_name, "sensor_log");
    EXPECT_EQ(schema.columns.size(), 4u);

    // Step 4: Schema validate
    auto validate_result = schema.validate();
    EXPECT_TRUE(validate_result.ok()) << "Schema validation failed";
}

// ============================================================================
// Smoke 2: Schema → RectangularSampler generate → ValueRangeValidator validate
// ============================================================================
TEST(SmokeTest, GenerateAndValidate) {
    // Build schema manually
    Schema schema;
    schema.type_name = "sensor";

    ColumnDef temp_col;
    temp_col.name = "temperature";
    temp_col.type = DataType::kFloat;
    temp_col.range_min = -50.0;
    temp_col.range_max = 80.0;
    schema.columns.push_back(temp_col);

    ColumnDef press_col;
    press_col.name = "pressure";
    press_col.type = DataType::kFloat;
    press_col.range_min = 900.0;
    press_col.range_max = 1100.0;
    schema.columns.push_back(press_col);

    ASSERT_TRUE(schema.validate().ok());

    // Generate data with constraints
    std::vector<ast::ConstraintItem> constraints = {
        {"temperature", ast::ConstraintOperator::kBetween, -10.0, 45.0},
        {"pressure", ast::ConstraintOperator::kBetween, 950.0, 1050.0}
    };

    GenerationRequest gen_req{schema, constraints, 500, 42, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << "Generate failed: " << gen_result.error().message;

    auto& gen_data = gen_result.value();
    EXPECT_EQ(gen_data.stats.rows_generated, 500);
    EXPECT_NE(gen_data.data, nullptr);
    EXPECT_EQ(gen_data.data->num_rows(), 500);

    // Validate with ValueRangeValidator
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_data.data);
    ASSERT_TRUE(val_result.ok()) << "Validation failed: " << val_result.error().message;

    auto& validation = val_result.value();
    EXPECT_EQ(validation.rows_checked, 500);
    // All generated data should pass value range (sampler produces within constraint)
    EXPECT_EQ(validation.rows_passed, 500);
    EXPECT_EQ(validation.rows_failed, 0);
    EXPECT_DOUBLE_EQ(validation.pass_rate, 1.0);
}

// ============================================================================
// Smoke 3: Generate → TailReport → EvidencePackage → JSON round-trip
// ============================================================================
TEST(SmokeTest, EvidencePackageJsonRoundTrip) {
    // Build schema
    Schema schema;
    schema.type_name = "evidence_test";

    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    schema.columns.push_back(col);

    ASSERT_TRUE(schema.validate().ok());

    // Generate
    std::vector<ast::ConstraintItem> constraints = {
        {"value", ast::ConstraintOperator::kBetween, 10.0, 90.0}
    };
    GenerationRequest gen_req{schema, constraints, 200, 12345, "uniform", 1000};

    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // Validate
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    // Build tail report
    TailReportBuilder tr_builder;
    auto tail_result = tr_builder.build(
        gen_result.value(), val_result.value(), gen_req, constraints);
    ASSERT_TRUE(tail_result.ok()) << tail_result.error().message;

    auto& tail_report = tail_result.value();
    EXPECT_EQ(tail_report.rows_generated, 200);
    EXPECT_EQ(tail_report.distribution_used, "uniform");
    EXPECT_EQ(tail_report.seed_used, 12345u);

    // Build provenance
    ProvenanceV1 provenance;
    provenance.data_source = "smoke_test";
    provenance.generator_identity = "RectangularSampler";
    provenance.constraints = {"value_range"};
    provenance.generation_params = GenerationParams{12345, "uniform", 200, 1000};

    // Build evidence package
    EvidencePackageBuilder ep_builder;
    auto ep_result = ep_builder.build(
        gen_result.value(), val_result.value(), tail_report, provenance, schema);
    ASSERT_TRUE(ep_result.ok()) << ep_result.error().message;

    auto& ep = ep_result.value();
    EXPECT_EQ(ep.schema_version, "v1");
    EXPECT_EQ(ep.row_count, 200);
    EXPECT_FALSE(ep.schema_hash.empty());

    // JSON serialize
    auto json_result = ep_builder.to_json(ep);
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;
    auto& json_str = json_result.value();
    EXPECT_GT(json_str.size(), 0u);

    // JSON deserialize round-trip
    auto rt_result = ep_builder.from_json(json_str);
    ASSERT_TRUE(rt_result.ok()) << rt_result.error().message;

    auto& ep_rt = rt_result.value();
    EXPECT_EQ(ep_rt.schema_version, ep.schema_version);
    EXPECT_EQ(ep_rt.schema_hash, ep.schema_hash);
    EXPECT_EQ(ep_rt.row_count, ep.row_count);
    EXPECT_EQ(ep_rt.data_grade, ep.data_grade);
    EXPECT_DOUBLE_EQ(ep_rt.exclusion_rate, ep.exclusion_rate);
    EXPECT_EQ(ep_rt.provenance.generator_identity, ep.provenance.generator_identity);

    // Also test free-function JSON round-trip
    auto json2 = to_json(ep);
    ASSERT_TRUE(json2.ok());
    auto ep_rt2 = from_json(json2.value());
    ASSERT_TRUE(ep_rt2.ok());
    EXPECT_EQ(ep_rt2.value().row_count, ep.row_count);
}

// ============================================================================
// Smoke 4: Generate → ObjectStoreBackend append → scan readback
// ============================================================================
TEST(SmokeTest, StorageRoundTrip) {
    // Create temp directory for storage
    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "synthgen_smoke_test";
    std::filesystem::remove_all(tmp_dir);
    std::filesystem::create_directories(tmp_dir);

    // Build schema
    Schema schema;
    schema.type_name = "storage_test";

    ColumnDef col;
    col.name = "measurement";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    schema.columns.push_back(col);

    ASSERT_TRUE(schema.validate().ok());

    // Generate data
    std::vector<ast::ConstraintItem> constraints = {
        {"measurement", ast::ConstraintOperator::kBetween, 20.0, 80.0}
    };
    GenerationRequest gen_req{schema, constraints, 100, 99, "uniform", 1000};

    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->num_rows(), 100);

    // Save first few values for later comparison
    auto col_arr = std::static_pointer_cast<arrow::DoubleArray>(table->column(0)->chunk(0));
    double first_val = col_arr->Value(0);
    double last_val = col_arr->Value(col_arr->length() - 1);

    // Write to storage
    ObjectStoreBackend backend(tmp_dir);
    auto reg_result = backend.register_table("storage_test", "{}");
    ASSERT_TRUE(reg_result.ok()) << reg_result.error().message;

    auto append_result = backend.append("storage_test", table);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Read back via scan
    auto scan_result = backend.scan("storage_test");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto read_table = scan_result.value();
    ASSERT_NE(read_table, nullptr);
    EXPECT_EQ(read_table->num_rows(), 100);

    // Verify data integrity — first and last values match
    auto read_arr = std::static_pointer_cast<arrow::DoubleArray>(
        read_table->column(0)->chunk(0));
    ASSERT_EQ(read_arr->length(), col_arr->length());
    EXPECT_DOUBLE_EQ(read_arr->Value(0), first_val);
    EXPECT_DOUBLE_EQ(read_arr->Value(read_arr->length() - 1), last_val);

    // Verify table exists
    auto has = backend.has_table("storage_test");
    ASSERT_TRUE(has.ok());
    EXPECT_TRUE(has.value());

    // Cleanup
    std::filesystem::remove_all(tmp_dir);
}

// ============================================================================
// Smoke 5: AuditLog create_genesis → append steps → verify_chain → daily_verification
// ============================================================================
TEST(SmokeTest, AuditLogChainIntegrity) {
    AuditLog log;

    // Step 1: Create genesis record
    auto genesis_result = log.create_genesis();
    ASSERT_TRUE(genesis_result.ok()) << genesis_result.error().message;
    EXPECT_EQ(log.record_count(), 1);

    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "genesis");
    EXPECT_FALSE(latest.value().chain_hash.empty());

    // Step 2: Append multiple operation records
    std::map<std::string, std::string> meta1 = {{"table", "sensor_log"}, {"rows", "1000"}};
    auto append1 = log.append("data_generation", "SynthGenEngine", meta1);
    ASSERT_TRUE(append1.ok()) << append1.error().message;
    EXPECT_EQ(log.record_count(), 2);
    EXPECT_EQ(append1.value().operation, "data_generation");
    EXPECT_EQ(append1.value().actor_identity, "SynthGenEngine");

    std::map<std::string, std::string> meta2 = {{"constraint", "temp_check"}};
    auto append2 = log.append("validation", "ValueRangeValidator", meta2);
    ASSERT_TRUE(append2.ok()) << append2.error().message;
    EXPECT_EQ(log.record_count(), 3);

    std::map<std::string, std::string> meta3 = {{"format", "json"}, {"size", "2048"}};
    auto append3 = log.append("evidence_export", "EvidencePackageBuilder", meta3);
    ASSERT_TRUE(append3.ok()) << append3.error().message;
    EXPECT_EQ(log.record_count(), 4);

    // Step 3: Verify chain integrity
    auto verify_result = log.verify_chain();
    ASSERT_TRUE(verify_result.ok()) << verify_result.error().message;
    EXPECT_TRUE(verify_result.value()) << "Chain verification failed";

    // Step 4: Daily verification report
    auto daily_result = log.daily_verification();
    ASSERT_TRUE(daily_result.ok()) << daily_result.error().message;
    auto& report = daily_result.value();
    EXPECT_TRUE(report.is_valid);
    EXPECT_EQ(report.total_records, 4);
    // daily_verification verifies chain links between records (not genesis itself),
    // so 4 records → 3 verified links
    EXPECT_EQ(report.verified_records, 3);
    EXPECT_TRUE(report.broken_links.empty());
    EXPECT_TRUE(report.fork_points.empty());

    // Step 5: Verify latest record is the last appended
    auto latest2 = log.get_latest();
    ASSERT_TRUE(latest2.ok());
    EXPECT_EQ(latest2.value().operation, "evidence_export");
    EXPECT_FALSE(latest2.value().chain_hash.empty());
    EXPECT_FALSE(latest2.value().prev_hash.empty());

    // Step 6: Scan all records
    auto scan_result = log.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(scan_result.ok());
    EXPECT_EQ(scan_result.value().size(), 4u);
}
