// Chaos test round 2: Schema validation, Evidence Package, cross-component edge cases
// These tests target edge conditions that standard unit/integration tests often miss.

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "schema/schema.h"
#include "schema/schema_builder.h"
#include "schema/schema_registry.h"
#include "engine/evidence/evidence_package.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/schema_validator.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "parser/ast.h"
#include "common/result.h"
#include "common/types.h"

using namespace synthgen;
using namespace synthgen::schema;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;

// ============================================================================
// Helpers
// ============================================================================

static ColumnDef make_float_col(const std::string& name,
                                 double min_val,
                                 double max_val) {
    ColumnDef c;
    c.name = name;
    c.type = DataType::kFloat;
    c.range_min = min_val;
    c.range_max = max_val;
    return c;
}

static Schema make_simple_schema(const std::string& name = "test_type") {
    Schema s;
    s.type_name = name;
    s.columns.push_back(make_float_col("temp", -50.0, 80.0));
    return s;
}

static EvidencePackageV1 make_minimal_v1_pkg() {
    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = "abc123";
    pkg.constraint_summary.type = "value_range";
    pkg.exclusion_rate = 0.0;
    pkg.data_grade = "physics_guaranteed";
    pkg.row_count = 100;
    pkg.epistemological_bias = "physical_first";
    pkg.tail_exclusion_statement = "Tail events systematically excluded.";
    pkg.exclusion_rate_report = 0.0;
    pkg.rows_generated = 100;
    pkg.rows_validated = 100;
    pkg.rows_failed_validation = 0;
    pkg.distribution_used = "uniform";
    pkg.seed_used = 42;
    pkg.audit_immutability = "not_applicable";
    pkg.statistical_fidelity = "not_applicable";
    pkg.drift_detection = "not_applicable";
    pkg.constraint_type_breakdown = "not_applicable";
    return pkg;
}

// ============================================================================
// Test 1: Schema with 200 columns -- validate should work
// ============================================================================
TEST(SchemaEvidenceChaos, Schema200ColumnsValidates) {
    Schema s;
    s.type_name = "wide_type";
    for (int i = 0; i < 200; ++i) {
        s.columns.push_back(make_float_col("col_" + std::to_string(i),
                                            static_cast<double>(i),
                                            static_cast<double>(i + 1)));
    }
    auto result = s.validate();
    ASSERT_TRUE(result.ok()) << "200-column schema should validate: " << result.error().message;

    // Also verify find_column and column_index work at scale
    auto found = s.find_column("col_199");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "col_199");
    EXPECT_EQ(s.column_index("col_199"), 199);
}

// ============================================================================
// Test 2: Schema with column named "" (empty string) -- should error
// ============================================================================
TEST(SchemaEvidenceChaos, EmptyColumnNameErrors) {
    Schema s;
    s.type_name = "bad_col_name";
    ColumnDef c;
    c.name = "";  // empty
    c.type = DataType::kFloat;
    s.columns.push_back(c);
    auto result = s.validate();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidColumnName);
}

// ============================================================================
// Test 3: Schema validate with FLOAT column that has range_min=NaN
// ============================================================================
TEST(SchemaEvidenceChaos, RangeMinNaNBehavior) {
    Schema s;
    s.type_name = "nan_range";
    ColumnDef c;
    c.name = "val";
    c.type = DataType::kFloat;
    c.range_min = std::numeric_limits<double>::quiet_NaN();
    c.range_max = 100.0;
    s.columns.push_back(c);
    // NaN range bounds should be rejected (was a bug -- NaN >= max is false so it
    // previously slipped through validation).
    auto result = s.validate();
    ASSERT_FALSE(result.ok()) << "NaN range_min should be rejected by validate()";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

// ============================================================================
// Test 4: Schema where range_min == range_max (zero-width range)
// ============================================================================
TEST(SchemaEvidenceChaos, RangeMinEqualsRangeMax) {
    Schema s;
    s.type_name = "zero_width";
    s.columns.push_back(make_float_col("x", 5.0, 5.0));
    auto result = s.validate();
    ASSERT_FALSE(result.ok()) << "range_min == range_max should be rejected";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
}

// ============================================================================
// Test 5: SchemaBuilder with AST that has no columns -- should error
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaBuilderNoColumns) {
    parser::ast::DefineTypeStmt stmt;
    stmt.type_name = "empty_type";
    // columns vector is empty by default
    SchemaBuilder builder;
    auto result = builder.build(stmt);
    ASSERT_FALSE(result.ok()) << "SchemaBuilder should reject empty columns";
    // The builder calls schema.validate() which checks columns.empty()
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidSchema);
}

// ============================================================================
// Test 6: SchemaRegistry: register 100 schemas, verify all retrievable
// ============================================================================
TEST(SchemaEvidenceChaos, Registry100Schemas) {
    SchemaRegistry reg;
    for (int i = 0; i < 100; ++i) {
        Schema s;
        s.type_name = "type_" + std::to_string(i);
        s.columns.push_back(make_float_col("v", 0.0, static_cast<double>(i + 1)));
        auto r = reg.register_schema(std::move(s));
        ASSERT_TRUE(r.ok()) << "register " << i << " failed: " << r.error().message;
    }
    for (int i = 0; i < 100; ++i) {
        std::string name = "type_" + std::to_string(i);
        EXPECT_TRUE(reg.has_schema(name)) << "Missing schema: " << name;
        auto r = reg.get_schema(name);
        ASSERT_TRUE(r.ok()) << "get_schema(" << name << ") failed";
        EXPECT_EQ(r.value()->type_name, name);
        EXPECT_EQ(r.value()->columns.size(), 1u);
    }
}

// ============================================================================
// Test 7: SchemaRegistry: get_schema for nonexistent name -- should error
// ============================================================================
TEST(SchemaEvidenceChaos, RegistryGetNonexistent) {
    SchemaRegistry reg;
    Schema s = make_simple_schema("exists");
    ASSERT_TRUE(reg.register_schema(std::move(s)).ok());

    auto r = reg.get_schema("does_not_exist");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

// ============================================================================
// Test 8: Schema::find_column for column that doesn't exist -- returns nullopt
// ============================================================================
TEST(SchemaEvidenceChaos, FindColumnNonexistent) {
    Schema s = make_simple_schema("test");
    auto found = s.find_column("nonexistent_column");
    EXPECT_FALSE(found.has_value());
}

// ============================================================================
// Test 9: Schema::column_index for column that doesn't exist -- returns -1
// ============================================================================
TEST(SchemaEvidenceChaos, ColumnIndexNonexistent) {
    Schema s = make_simple_schema("test");
    int idx = s.column_index("nonexistent_column");
    EXPECT_EQ(idx, -1);
}

// ============================================================================
// Test 10: EvidencePackageV1 with extreme values
// ============================================================================
TEST(SchemaEvidenceChaos, ExtremeValuesInV1Package) {
    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = "extreme_hash";
    pkg.exclusion_rate = 1.0;  // 100% exclusion -- extreme but finite
    pkg.data_grade = "physics_guaranteed";
    pkg.row_count = std::numeric_limits<int64_t>::max();
    pkg.epistemological_bias = "physical_first";
    pkg.tail_exclusion_statement = "Extreme test.";
    pkg.exclusion_rate_report = 1.0;
    pkg.rows_generated = std::numeric_limits<int64_t>::max();
    pkg.rows_validated = std::numeric_limits<int64_t>::max();
    pkg.rows_failed_validation = 0;
    pkg.distribution_used = "uniform";
    pkg.seed_used = std::numeric_limits<uint64_t>::max();
    pkg.audit_immutability = "not_applicable";
    pkg.statistical_fidelity = "not_applicable";
    pkg.drift_detection = "not_applicable";
    pkg.constraint_type_breakdown = "not_applicable";

    // The honesty validator checks exclusion_rate == 0.0 and
    // rows_failed_validation == 0 for v1. exclusion_rate=1.0 will fail
    // honesty check, which is expected behavior.
    SchemaValidator validator;
    auto result = validator.validate(pkg);
    // exclusion_rate=1.0 violates v1 honesty (must be 0.0)
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kConsistencyError);
}

// ============================================================================
// Test 11: EvidencePackageV1 JSON round-trip -- build -> to_json -> from_json -> compare
// ============================================================================
TEST(SchemaEvidenceChaos, JsonRoundTrip) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    // Add some constraint details and provenance
    pkg.constraint_summary.details.push_back({"temp", -50.0, 80.0});
    pkg.provenance.data_source = "test_source";
    pkg.provenance.constraints.push_back("c1");
    pkg.provenance.constraints.push_back("c2");
    pkg.provenance.generation_params.seed = 42;
    pkg.provenance.generation_params.distribution = "uniform";
    pkg.provenance.generation_params.limit = 100;
    pkg.provenance.generation_params.batch_size = 1000;
    pkg.provenance.generator_identity = "rectangular_sampler";

    TraceSpanEntry span;
    span.trace_id = "t1";
    span.span_id = "s1";
    span.component = "physics";
    span.operation = "generate";
    span.status = "ok";
    pkg.provenance.trace_spans.push_back(span);

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;

    auto parsed_result = from_json(json_result.value());
    ASSERT_TRUE(parsed_result.ok()) << parsed_result.error().message;

    const auto& pkg2 = parsed_result.value();

    // Compare every field
    EXPECT_EQ(pkg.schema_version, pkg2.schema_version);
    EXPECT_EQ(pkg.schema_hash, pkg2.schema_hash);
    EXPECT_EQ(pkg.constraint_summary.type, pkg2.constraint_summary.type);
    ASSERT_EQ(pkg.constraint_summary.details.size(), pkg2.constraint_summary.details.size());
    for (size_t i = 0; i < pkg.constraint_summary.details.size(); ++i) {
        EXPECT_EQ(pkg.constraint_summary.details[i].column, pkg2.constraint_summary.details[i].column);
        EXPECT_DOUBLE_EQ(pkg.constraint_summary.details[i].min, pkg2.constraint_summary.details[i].min);
        EXPECT_DOUBLE_EQ(pkg.constraint_summary.details[i].max, pkg2.constraint_summary.details[i].max);
    }
    EXPECT_DOUBLE_EQ(pkg.exclusion_rate, pkg2.exclusion_rate);
    EXPECT_EQ(pkg.data_grade, pkg2.data_grade);
    EXPECT_EQ(pkg.row_count, pkg2.row_count);

    // Provenance
    EXPECT_EQ(pkg.provenance.data_source, pkg2.provenance.data_source);
    ASSERT_EQ(pkg.provenance.constraints.size(), pkg2.provenance.constraints.size());
    for (size_t i = 0; i < pkg.provenance.constraints.size(); ++i) {
        EXPECT_EQ(pkg.provenance.constraints[i], pkg2.provenance.constraints[i]);
    }
    EXPECT_EQ(pkg.provenance.generation_params.seed, pkg2.provenance.generation_params.seed);
    EXPECT_EQ(pkg.provenance.generation_params.distribution, pkg2.provenance.generation_params.distribution);
    EXPECT_EQ(pkg.provenance.generation_params.limit, pkg2.provenance.generation_params.limit);
    EXPECT_EQ(pkg.provenance.generation_params.batch_size, pkg2.provenance.generation_params.batch_size);
    EXPECT_EQ(pkg.provenance.generator_identity, pkg2.provenance.generator_identity);
    ASSERT_EQ(pkg.provenance.trace_spans.size(), pkg2.provenance.trace_spans.size());
    for (size_t i = 0; i < pkg.provenance.trace_spans.size(); ++i) {
        EXPECT_EQ(pkg.provenance.trace_spans[i].trace_id, pkg2.provenance.trace_spans[i].trace_id);
        EXPECT_EQ(pkg.provenance.trace_spans[i].span_id, pkg2.provenance.trace_spans[i].span_id);
        EXPECT_EQ(pkg.provenance.trace_spans[i].component, pkg2.provenance.trace_spans[i].component);
        EXPECT_EQ(pkg.provenance.trace_spans[i].operation, pkg2.provenance.trace_spans[i].operation);
        EXPECT_EQ(pkg.provenance.trace_spans[i].status, pkg2.provenance.trace_spans[i].status);
    }

    // Tail report fields
    EXPECT_EQ(pkg.epistemological_bias, pkg2.epistemological_bias);
    EXPECT_EQ(pkg.tail_exclusion_statement, pkg2.tail_exclusion_statement);
    EXPECT_DOUBLE_EQ(pkg.exclusion_rate_report, pkg2.exclusion_rate_report);
    EXPECT_EQ(pkg.rows_generated, pkg2.rows_generated);
    EXPECT_EQ(pkg.rows_validated, pkg2.rows_validated);
    EXPECT_EQ(pkg.rows_failed_validation, pkg2.rows_failed_validation);
    EXPECT_EQ(pkg.distribution_used, pkg2.distribution_used);
    EXPECT_EQ(pkg.seed_used, pkg2.seed_used);

    // Applicability fields
    EXPECT_EQ(pkg.audit_immutability, pkg2.audit_immutability);
    EXPECT_EQ(pkg.statistical_fidelity, pkg2.statistical_fidelity);
    EXPECT_EQ(pkg.drift_detection, pkg2.drift_detection);
    EXPECT_EQ(pkg.constraint_type_breakdown, pkg2.constraint_type_breakdown);
}

// ============================================================================
// Test 12: TailReportBuilder with 0 rows generated
// ============================================================================
TEST(SchemaEvidenceChaos, TailReportZeroRows) {
    // Construct a GenerationResult with 0 rows
    GenerationResult gen_result;
    gen_result.data = nullptr;
    gen_result.stats.rows_generated = 0;
    gen_result.stats.exclusion_rate = 0.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 0;
    val_result.rows_passed = 0;
    val_result.rows_failed = 0;
    val_result.pass_rate = 1.0;

    Schema zero_schema = make_simple_schema("zero_row_type");
    GenerationRequest request{zero_schema, {}, 0, 12345};

    TailReportBuilder builder;
    auto result = builder.build(gen_result, val_result, request, {});
    ASSERT_TRUE(result.ok()) << "TailReport with 0 rows should succeed: " << result.error().message;

    const auto& report = result.value();
    EXPECT_EQ(report.rows_generated, 0);
    EXPECT_EQ(report.rows_validated, 0);
    EXPECT_EQ(report.rows_failed_validation, 0);
    EXPECT_DOUBLE_EQ(report.total_exclusion_rate, 0.0);
}

// ============================================================================
// Test 13: TailReportBuilder with 100% pass rate
// ============================================================================
TEST(SchemaEvidenceChaos, TailReportAllPass) {
    GenerationResult gen_result;
    gen_result.data = nullptr;
    gen_result.stats.rows_generated = 1000;
    gen_result.stats.exclusion_rate = 0.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 1000;
    val_result.rows_passed = 1000;
    val_result.rows_failed = 0;
    val_result.pass_rate = 1.0;

    Schema pass_schema = make_simple_schema("all_pass_type");
    GenerationRequest request{pass_schema, {}, 1000, 99};

    TailReportBuilder builder;
    auto result = builder.build(gen_result, val_result, request, {});
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& report = result.value();
    EXPECT_EQ(report.rows_generated, 1000);
    EXPECT_EQ(report.rows_validated, 1000);
    EXPECT_EQ(report.rows_failed_validation, 0);
}

// ============================================================================
// Test 14: TailReportBuilder with 0% pass rate (all fail)
// ============================================================================
TEST(SchemaEvidenceChaos, TailReportAllFail) {
    GenerationResult gen_result;
    gen_result.data = nullptr;
    gen_result.stats.rows_generated = 500;
    gen_result.stats.exclusion_rate = 1.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 500;
    val_result.rows_passed = 0;
    val_result.rows_failed = 500;
    val_result.pass_rate = 0.0;

    Schema fail_schema = make_simple_schema("all_fail_type");
    GenerationRequest request{fail_schema, {}, 500, 77};

    TailReportBuilder builder;
    auto result = builder.build(gen_result, val_result, request, {});
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& report = result.value();
    EXPECT_EQ(report.rows_generated, 500);
    EXPECT_EQ(report.rows_validated, 500);
    EXPECT_EQ(report.rows_failed_validation, 500);
    // The exclusion_rate comes from generation_result.stats.exclusion_rate
    EXPECT_DOUBLE_EQ(report.total_exclusion_rate, 1.0);
}

// ============================================================================
// Test 15: SchemaValidator with wrong schema_hash -- validate still passes
//          (SchemaValidator only checks format/honesty, not hash correctness)
//          But verify that hash mismatch is detectable by computing hash ourselves.
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaHashMismatchDetectable) {
    // Build two schemas with different structures -> different hashes
    Schema s1;
    s1.type_name = "sensor_a";
    s1.columns.push_back(make_float_col("temp", -10.0, 50.0));

    Schema s2;
    s2.type_name = "sensor_a";
    s2.columns.push_back(make_float_col("temp", -20.0, 60.0));  // different range

    auto hash1 = EvidencePackageBuilder::compute_schema_hash(s1);
    auto hash2 = EvidencePackageBuilder::compute_schema_hash(s2);

    EXPECT_NE(hash1, hash2) << "Different schemas must produce different hashes";

    // Now create an evidence package with the wrong hash
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.schema_hash = hash1;  // hash for s1

    SchemaValidator validator;
    auto result = validator.validate(pkg);
    // SchemaValidator should pass -- it checks required fields and honesty,
    // not the actual hash correctness. The hash is a declaration.
    EXPECT_TRUE(result.ok()) << "SchemaValidator checks format, not hash correctness: "
                             << result.error().message;

    // But we can detect the mismatch externally:
    std::string wrong_hash = hash2;
    bool mismatch = (pkg.schema_hash != wrong_hash);
    EXPECT_TRUE(mismatch) << "Hash mismatch should be detectable externally";
}

// ============================================================================
// Bonus: SchemaValidator rejects empty schema_version (required field check)
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaValidatorRejectsEmptyVersion) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.schema_version = "";
    SchemaValidator validator;
    auto result = validator.validate(pkg);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

// ============================================================================
// Bonus: SchemaValidator rejects NaN exclusion_rate
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaValidatorRejectsNaNExclusionRate) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.exclusion_rate = std::numeric_limits<double>::quiet_NaN();
    SchemaValidator validator;
    auto result = validator.validate(pkg);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

// ============================================================================
// Bonus: SchemaValidator rejects v1 with non-"not_applicable" applicability
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaValidatorRejectsWrongApplicability) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.audit_immutability = "verified";  // not valid for v1
    SchemaValidator validator;
    auto result = validator.validate(pkg);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

// ============================================================================
// Bonus: JSON round-trip with empty string fields
// ============================================================================
TEST(SchemaEvidenceChaos, JsonRoundTripEmptyStrings) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.provenance.data_source = "";
    pkg.provenance.generator_identity = "";
    pkg.distribution_used = "";
    // tail_exclusion_statement stays non-empty (required)

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());

    auto parsed_result = from_json(json_result.value());
    ASSERT_TRUE(parsed_result.ok());

    const auto& pkg2 = parsed_result.value();
    EXPECT_EQ(pkg2.provenance.data_source, "");
    EXPECT_EQ(pkg2.provenance.generator_identity, "");
    EXPECT_EQ(pkg2.distribution_used, "");
}

// ============================================================================
// Bonus: SchemaBuilder rejects duplicate column names
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaBuilderDuplicateColumns) {
    parser::ast::DefineTypeStmt stmt;
    stmt.type_name = "dup_cols";
    parser::ast::ColumnDef col1;
    col1.name = "x";
    col1.type = DataType::kFloat;
    col1.range_min = 0.0;
    col1.range_max = 10.0;
    parser::ast::ColumnDef col2;
    col2.name = "x";  // duplicate
    col2.type = DataType::kInt;
    stmt.columns.push_back(col1);
    stmt.columns.push_back(col2);

    SchemaBuilder builder;
    auto result = builder.build(stmt);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kDuplicateColumnName);
}

// ============================================================================
// Bonus: SchemaRegistry rejects duplicate type_name registration
// ============================================================================
TEST(SchemaEvidenceChaos, RegistryDuplicateTypeName) {
    SchemaRegistry reg;
    Schema s1 = make_simple_schema("dup");
    Schema s2 = make_simple_schema("dup");
    ASSERT_TRUE(reg.register_schema(std::move(s1)).ok());
    auto r2 = reg.register_schema(std::move(s2));
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, ErrorCode::kDuplicateTypeName);
}

// ============================================================================
// Bonus: SchemaValidator rejects empty data_grade
// ============================================================================
TEST(SchemaEvidenceChaos, SchemaValidatorRejectsEmptyDataGrade) {
    EvidencePackageV1 pkg = make_minimal_v1_pkg();
    pkg.data_grade = "";
    SchemaValidator validator;
    auto result = validator.validate(pkg);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

// ============================================================================
// Bonus: Schema with ENUM column but empty enum_values
// ============================================================================
TEST(SchemaEvidenceChaos, EnumColumnEmptyValues) {
    Schema s;
    s.type_name = "bad_enum";
    ColumnDef c;
    c.name = "status";
    c.type = DataType::kEnum;
    c.enum_values = {};  // empty
    s.columns.push_back(c);
    auto result = s.validate();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidEnum);
}

// ============================================================================
// Bonus: Schema with ORDER column referencing a column that exists
// ============================================================================
TEST(SchemaEvidenceChaos, OrderColumnValidatesCorrectly) {
    Schema s;
    s.type_name = "ordered_type";
    ColumnDef c;
    c.name = "ts";
    c.type = DataType::kDatetime;
    c.is_order = true;
    c.not_null = true;
    s.columns.push_back(c);
    s.columns.push_back(make_float_col("val", 0.0, 1.0));

    auto result = s.validate();
    EXPECT_TRUE(result.ok()) << "ORDER column present in columns should validate: "
                             << (result.ok() ? "" : result.error().message);

    auto order_cols = s.order_columns();
    ASSERT_EQ(order_cols.size(), 1u);
    EXPECT_EQ(order_cols[0], "ts");
}
