#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "engine/evidence/evidence_package.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/evidence/schema_validator.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "schema/schema.h"
#include "common/hash.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <cmath>
#include <string>

using namespace synthgen;
using namespace synthgen::engine::evidence;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::schema;
using namespace synthgen::scaffold;

// Helper: create a valid EvidencePackageV1 for testing
EvidencePackageV1 make_valid_package() {
    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = "abc123hash";
    pkg.constraint_summary.type = "value_range";
    pkg.constraint_summary.details = {{"temperature", -10.0, 45.0}};
    pkg.exclusion_rate = 0.0;
    pkg.data_grade = "physics_guaranteed";
    pkg.row_count = 100;
    pkg.provenance.data_source = "/data/test.parquet";
    pkg.provenance.constraints = {"safe_range"};
    pkg.provenance.generation_params = {42, "uniform", 100, 1000};
    pkg.provenance.generator_identity = "physics_sampler";
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

Schema make_simple_schema() {
    Schema s;
    s.type_name = "sensor_log";
    ColumnDef col;
    col.name = "temperature";
    col.type = DataType::kFloat;
    col.range_min = -10.0;
    col.range_max = 45.0;
    s.columns.push_back(col);
    return s;
}

// ===== Data Structure Tests =====

TEST(EvidencePackageTest, DefaultValues) {
    EvidencePackageV1 pkg;
    EXPECT_EQ(pkg.schema_version, "v1");
    EXPECT_EQ(pkg.data_grade, "physics_guaranteed");
    EXPECT_DOUBLE_EQ(pkg.exclusion_rate, 0.0);
    EXPECT_EQ(pkg.row_count, 0);
    EXPECT_EQ(pkg.audit_immutability, "not_applicable");
    EXPECT_EQ(pkg.statistical_fidelity, "not_applicable");
    EXPECT_EQ(pkg.drift_detection, "not_applicable");
    EXPECT_EQ(pkg.constraint_type_breakdown, "not_applicable");
}

TEST(EvidencePackageTest, FieldAssignment) {
    auto pkg = make_valid_package();
    EXPECT_EQ(pkg.schema_hash, "abc123hash");
    EXPECT_EQ(pkg.constraint_summary.type, "value_range");
    EXPECT_EQ(pkg.constraint_summary.details.size(), 1u);
    EXPECT_EQ(pkg.constraint_summary.details[0].column, "temperature");
    EXPECT_DOUBLE_EQ(pkg.constraint_summary.details[0].min, -10.0);
    EXPECT_DOUBLE_EQ(pkg.constraint_summary.details[0].max, 45.0);
}

// ===== JSON Serialization Tests =====

TEST(EvidencePackageJsonTest, SerializeAndDeserialize) {
    auto pkg = make_valid_package();
    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok()) << json_result.error().message;

    auto parse_result = from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok()) << parse_result.error().message;

    const auto& pkg2 = parse_result.value();
    EXPECT_EQ(pkg2.schema_version, pkg.schema_version);
    EXPECT_EQ(pkg2.schema_hash, pkg.schema_hash);
    EXPECT_DOUBLE_EQ(pkg2.exclusion_rate, pkg.exclusion_rate);
    EXPECT_EQ(pkg2.data_grade, pkg.data_grade);
    EXPECT_EQ(pkg2.row_count, pkg.row_count);
    EXPECT_EQ(pkg2.audit_immutability, pkg.audit_immutability);
}

TEST(EvidencePackageJsonTest, RoundTripPreservesFields) {
    auto pkg = make_valid_package();
    pkg.provenance.trace_spans = {{"tid1", "sid1", "parser", "parse", "ok"}};

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    auto parse_result = from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok());

    const auto& pkg2 = parse_result.value();
    EXPECT_EQ(pkg2.epistemological_bias, "physical_first");
    EXPECT_EQ(pkg2.tail_exclusion_statement, "Tail events systematically excluded.");
    EXPECT_EQ(pkg2.provenance.trace_spans.size(), 1u);
    EXPECT_EQ(pkg2.provenance.trace_spans[0].trace_id, "tid1");
    EXPECT_EQ(pkg2.provenance.generation_params.seed, 42u);
}

TEST(EvidencePackageJsonTest, SpecialCharactersEscaped) {
    auto pkg = make_valid_package();
    pkg.provenance.data_source = "/data/test \"quoted\" & <tagged>";

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());

    auto parse_result = from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok());
    EXPECT_EQ(parse_result.value().provenance.data_source, pkg.provenance.data_source);
}

TEST(EvidencePackageJsonTest, DeserializeInvalidJson) {
    auto result = from_json("not valid json {{{");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kDeserializationError);
}

TEST(EvidencePackageJsonTest, DeserializeNonObject) {
    auto result = from_json("[1, 2, 3]");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kDeserializationError);
}

TEST(EvidencePackageJsonTest, EmptyConstraintSummary) {
    auto pkg = make_valid_package();
    pkg.constraint_summary.details.clear();

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    EXPECT_NE(json_result.value().find("\"details\":[]"), std::string::npos);
}

TEST(EvidencePackageJsonTest, EmptyTraceSpans) {
    auto pkg = make_valid_package();
    pkg.provenance.trace_spans.clear();

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    auto parse_result = from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok());
    EXPECT_TRUE(parse_result.value().provenance.trace_spans.empty());
}

// ===== Schema Validator Tests =====

TEST(SchemaValidatorTest, ValidPackagePasses) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    auto result = validator.validate(pkg);
    EXPECT_TRUE(result.ok()) << result.error().message;
}

TEST(SchemaValidatorTest, MissingSchemaVersion) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.schema_version.clear();
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

TEST(SchemaValidatorTest, MissingSchemaHash) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.schema_hash.clear();
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

TEST(SchemaValidatorTest, AuditImmutabilityViolation) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.audit_immutability = "verified";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, StatisticalFidelityViolation) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.statistical_fidelity = "high";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, DriftDetectionViolation) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.drift_detection = "none_detected";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, ConstraintTypeBreakdownViolation) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.constraint_type_breakdown = "complete";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, EpistemologicalBiasWrong) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.epistemological_bias = "data_driven";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, DataGradeWrong) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.data_grade = "statistically_validated";
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kHonestyViolation);
}

TEST(SchemaValidatorTest, ExclusionRateNonZero) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.exclusion_rate = 0.05;
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kConsistencyError);
}

TEST(SchemaValidatorTest, RowsFailedNonZero) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.rows_failed_validation = 5;
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kConsistencyError);
}

TEST(SchemaValidatorTest, TailExclusionStatementEmpty) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.tail_exclusion_statement.clear();
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

TEST(SchemaValidatorTest, ExclusionRateNaN) {
    SchemaValidator validator;
    auto pkg = make_valid_package();
    pkg.exclusion_rate = std::nan("");
    auto result = validator.validate(pkg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kSchemaViolation);
}

// ===== SHA256 Hash Tests =====

TEST(HashTest, ConsistentHash) {
    std::string h1 = sha256_hex("hello world");
    std::string h2 = sha256_hex("hello world");
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 64u);
}

TEST(HashTest, DifferentInputsDifferentHash) {
    std::string h1 = sha256_hex("hello");
    std::string h2 = sha256_hex("world");
    EXPECT_NE(h1, h2);
}

TEST(HashTest, KnownHashValue) {
    // SHA256("abc") = ba7816bf...
    std::string h = sha256_hex("abc");
    EXPECT_EQ(h.substr(0, 8), "ba7816bf");
    EXPECT_EQ(h.size(), 64u);
}

// ===== EvidencePackageBuilder Tests =====

TEST(EvidencePackageBuilderTest, ComputeSchemaHash) {
    Schema s = make_simple_schema();
    std::string h = EvidencePackageBuilder::compute_schema_hash(s);
    EXPECT_EQ(h.size(), 64u);

    // Same schema -> same hash
    std::string h2 = EvidencePackageBuilder::compute_schema_hash(s);
    EXPECT_EQ(h, h2);
}

TEST(EvidencePackageBuilderTest, SchemaHashDiffersForDifferentSchemas) {
    Schema s1 = make_simple_schema();
    Schema s2;
    s2.type_name = "other_type";
    s2.columns = s1.columns;

    std::string h1 = EvidencePackageBuilder::compute_schema_hash(s1);
    std::string h2 = EvidencePackageBuilder::compute_schema_hash(s2);
    EXPECT_NE(h1, h2);
}

// ===== Integration: Builder + JSON + Validator =====

TEST(EvidencePackageBuilderTest, ToJsonFromJsonRoundTrip) {
    EvidencePackageBuilder builder;
    auto pkg = make_valid_package();

    auto json_result = builder.to_json(pkg);
    ASSERT_TRUE(json_result.ok());

    auto parse_result = builder.from_json(json_result.value());
    ASSERT_TRUE(parse_result.ok());

    // Validate the round-tripped package
    auto vr = builder.validate_schema(parse_result.value());
    EXPECT_TRUE(vr.ok()) << vr.error().message;
}

TEST(EvidencePackageBuilderTest, SchemaHashMismatchDetected) {
    // Use builder to produce a real hash
    EvidencePackageBuilder builder;
    Schema s = make_simple_schema();
    std::string real_hash = EvidencePackageBuilder::compute_schema_hash(s);
    EXPECT_EQ(real_hash.size(), 64u);

    // A different schema should produce a different hash
    Schema s2;
    s2.type_name = "other";
    s2.columns = s.columns;
    std::string other_hash = EvidencePackageBuilder::compute_schema_hash(s2);
    EXPECT_NE(real_hash, other_hash);
}

// ===== Scaffold Integration Tests =====

TEST(EvidencePackageBuilderTest, BuildProducesTraceSpan) {
    scaffold::SpanGuard::active_spans().clear();

    EvidencePackageBuilder builder;
    Schema s = make_simple_schema();

    GenerationResult gen_result;
    gen_result.stats.rows_generated = 100;
    gen_result.stats.exclusion_rate = 0.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 100;
    val_result.rows_passed = 100;

    TailReportV1 tail;
    tail.data_grade = "physics_guaranteed";
    tail.rows_generated = 100;
    tail.rows_validated = 100;
    tail.distribution_used = "uniform";
    tail.seed_used = 42;

    ProvenanceV1 prov;
    prov.data_source = "/data/test.parquet";
    prov.generator_identity = "physics_sampler";
    prov.generation_params = {42, "uniform", 100, 1000};

    auto result = builder.build(gen_result, val_result, tail, prov, s);
    ASSERT_TRUE(result.ok()) << result.error().message;

    bool found_build_span = false;
    bool found_validate_span = false;
    for (const auto& sp : scaffold::SpanGuard::active_spans()) {
        if (sp.component == "evidence" && sp.operation == "build") found_build_span = true;
        if (sp.component == "evidence" && sp.operation == "validate") found_validate_span = true;
    }
    EXPECT_TRUE(found_build_span);
    EXPECT_TRUE(found_validate_span);
}

TEST(EvidencePackageBuilderTest, BuildUpdatesMetrics) {
    MetricsRegistry::instance().reset();

    EvidencePackageBuilder builder;
    Schema s = make_simple_schema();

    GenerationResult gen_result;
    gen_result.stats.exclusion_rate = 0.0;

    ValidationResult val_result;
    TailReportV1 tail;
    ProvenanceV1 prov;
    prov.generation_params = {0, "uniform", 0, 1000};

    auto result = builder.build(gen_result, val_result, tail, prov, s);
    ASSERT_TRUE(result.ok());

    auto counters = MetricsRegistry::instance().all_counters();
    EXPECT_GE(counters["evidence_package_total"], 1);
}

TEST(EvidencePackageBuilderTest, MinimalEvidencePackage) {
    EvidencePackageBuilder builder;
    Schema s;
    s.type_name = "minimal";
    // No columns, no constraints

    GenerationResult gen_result;
    gen_result.stats.exclusion_rate = 0.0;

    ValidationResult val_result;
    TailReportV1 tail;
    tail.data_grade = "physics_guaranteed";
    ProvenanceV1 prov;
    prov.generation_params = {0, "uniform", 0, 1000};

    auto result = builder.build(gen_result, val_result, tail, prov, s);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().constraint_summary.details.size(), 0u);
    EXPECT_EQ(result.value().row_count, 0);
}

TEST(EvidencePackageBuilderTest, BuildFillsAllFields) {
    EvidencePackageBuilder builder;
    Schema s = make_simple_schema();

    GenerationResult gen_result;
    gen_result.stats.rows_generated = 50;
    gen_result.stats.exclusion_rate = 0.0;
    gen_result.stats.distribution_used = "uniform";

    ValidationResult val_result;
    val_result.rows_checked = 50;
    val_result.rows_passed = 50;

    TailReportV1 tail;
    tail.data_grade = "physics_guaranteed";
    tail.rows_generated = 50;
    tail.rows_validated = 50;
    tail.distribution_used = "uniform";
    tail.seed_used = 123;

    ProvenanceV1 prov;
    prov.data_source = "/data/sensors.parquet";
    prov.constraints = {"safe_range"};
    prov.generation_params = {123, "uniform", 50, 1000};
    prov.generator_identity = "physics_sampler";

    auto result = builder.build(gen_result, val_result, tail, prov, s);
    ASSERT_TRUE(result.ok()) << result.error().message;

    const auto& pkg = result.value();
    EXPECT_EQ(pkg.schema_version, "v1");
    EXPECT_FALSE(pkg.schema_hash.empty());
    EXPECT_EQ(pkg.constraint_summary.type, "value_range");
    EXPECT_EQ(pkg.constraint_summary.details.size(), 1u);
    EXPECT_DOUBLE_EQ(pkg.exclusion_rate, 0.0);
    EXPECT_EQ(pkg.data_grade, "physics_guaranteed");
    EXPECT_EQ(pkg.audit_immutability, "not_applicable");
    EXPECT_EQ(pkg.statistical_fidelity, "not_applicable");
    EXPECT_EQ(pkg.drift_detection, "not_applicable");
    EXPECT_EQ(pkg.constraint_type_breakdown, "not_applicable");
    EXPECT_EQ(pkg.epistemological_bias, "physical_first");
    EXPECT_FALSE(pkg.tail_exclusion_statement.empty());
    EXPECT_EQ(pkg.rows_generated, 50);
    EXPECT_EQ(pkg.seed_used, 123u);
    EXPECT_EQ(pkg.provenance.data_source, "/data/sensors.parquet");
}
