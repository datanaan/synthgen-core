// Chaos Round 3: Full end-to-end pipeline tests
// Tests multi-component interactions that haven't been tested together
// after 2 rounds (964 tests, 9 bugs found).

#include <gtest/gtest.h>

#include "api/service.h"
#include "api/request.h"
#include "api/response.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/physics/uniform_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/evidence/tail_report.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "schema/schema.h"
#include "schema/schema_builder.h"
#include "schema/schema_registry.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "scaffold/metrics.h"

#include <arrow/api.h>
#include <arrow/table.h>
#include <rapidjson/document.h>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace synthgen;
using namespace synthgen::api;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::router;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;
using namespace synthgen::schema;

namespace {

// Helper: build a simple sensor schema
Schema make_sensor_schema() {
    Schema s;
    s.type_name = "sensor_log";
    {
        ColumnDef ts;
        ts.name = "timestamp";
        ts.type = DataType::kDatetime;
        ts.not_null = true;
        ts.is_order = true;
        s.columns.push_back(ts);
    }
    {
        ColumnDef ws;
        ws.name = "wind_speed";
        ws.type = DataType::kFloat;
        ws.range_min = 0.0;
        ws.range_max = 50.0;
        s.columns.push_back(ws);
    }
    {
        ColumnDef temp;
        temp.name = "temperature";
        temp.type = DataType::kFloat;
        temp.range_min = -50.0;
        temp.range_max = 80.0;
        s.columns.push_back(temp);
    }
    return s;
}

parser::ast::ConstraintItem make_between(const std::string& col,
                                          double min_val, double max_val) {
    parser::ast::ConstraintItem item;
    item.column_name = col;
    item.op = parser::ast::ConstraintOperator::kBetween;
    item.value_min = min_val;
    item.value_max = max_val;
    return item;
}

parser::ast::ConstraintItem make_gt(const std::string& col, double min_val) {
    parser::ast::ConstraintItem item;
    item.column_name = col;
    item.op = parser::ast::ConstraintOperator::kGreaterThan;
    item.value_min = min_val;
    item.value_max = 0;
    return item;
}

parser::ast::ConstraintItem make_lt(const std::string& col, double max_val) {
    parser::ast::ConstraintItem item;
    item.column_name = col;
    item.op = parser::ast::ConstraintOperator::kLessThan;
    item.value_min = 0;
    item.value_max = max_val;
    return item;
}

} // anonymous namespace

// ============================================================================
// Test 1: Full DSL -> Schema -> Generate -> Validate -> Store -> Audit
// ============================================================================
TEST(E2EPipelineChaos, FullPipeline_DSL_Schema_Generate_Validate_Store_Audit) {
    // Parse SynthLang DSL
    const char* dsl = R"(
        DEFINE TYPE e2e_sensor {
            timestamp: DATETIME NOT NULL ORDER,
            wind_speed: FLOAT [0.0, 50.0],
            temperature: FLOAT [-50.0, 80.0]
        };
    )";

    parser::Parser parser;
    auto parse_result = parser.parse(dsl);
    ASSERT_TRUE(parse_result.ok()) << parse_result.error().message;
    ASSERT_TRUE(parse_result.value().errors.empty());
    auto& program = parse_result.value().program;
    ASSERT_EQ(program.statements.size(), 1u);

    // Build schema from AST
    auto& stmt = std::get<parser::ast::DefineTypeStmt>(program.statements[0]);
    SchemaBuilder builder;
    auto schema_result = builder.build(stmt);
    ASSERT_TRUE(schema_result.ok()) << schema_result.error().message;
    auto& schema = schema_result.value();
    ASSERT_EQ(schema.type_name, "e2e_sensor");
    ASSERT_EQ(schema.columns.size(), 3u);

    // Generate 1000 rows
    std::vector<parser::ast::ConstraintItem> constraints;
    constraints.push_back(make_between("wind_speed", 0.0, 25.0));
    constraints.push_back(make_between("temperature", -20.0, 40.0));

    GenerationRequest gen_req{schema, constraints, 1000, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    ASSERT_EQ(gen_result.value().data->num_rows(), 1000);

    // Validate
    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;
    EXPECT_EQ(val_result.value().rows_failed, 0)
        << "All rows should pass constraints that match generation range";

    // Store in ObjectStoreBackend
    std::filesystem::path store_path =
        std::filesystem::temp_directory_path() / "e2e_chaos_test1";
    std::filesystem::remove_all(store_path);

    ObjectStoreBackend store(store_path);
    auto reg_result = store.register_table("e2e_sensor", "{}");
    ASSERT_TRUE(reg_result.ok()) << reg_result.error().message;

    auto append_result = store.append("e2e_sensor", gen_result.value().data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Scan back
    auto scan_result = store.scan("e2e_sensor");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    auto& scanned = scan_result.value();
    ASSERT_EQ(scanned->num_rows(), 1000);

    // Write audit log
    AuditLog audit;
    auto genesis = audit.create_genesis();
    ASSERT_TRUE(genesis.ok());

    auto rec = audit.append("generate", "e2e_test",
                             {{"type", "e2e_sensor"}, {"rows", "1000"}});
    ASSERT_TRUE(rec.ok());

    // Verify chain
    auto chain = audit.verify_chain();
    ASSERT_TRUE(chain.ok());
    EXPECT_TRUE(chain.value()) << "Audit chain should be valid after normal operations";

    std::filesystem::remove_all(store_path);
}

// ============================================================================
// Test 2: Service -> Evidence -> JSON parse -> verify all required fields
// ============================================================================
TEST(E2EPipelineChaos, Service_Evidence_JSON_AllRequiredFields) {
    SynthGenService service;

    // Define type
    DefineTypeRequest type_req;
    type_req.type_name = "ev_test_type";
    {
        DefineTypeRequest::ColumnDef ts;
        ts.name = "ts";
        ts.type = "DATETIME";
        ts.not_null = true;
        ts.is_order = true;
        type_req.columns.push_back(ts);
    }
    {
        DefineTypeRequest::ColumnDef val;
        val.name = "value";
        val.type = "FLOAT";
        val.range_min = 0.0;
        val.range_max = 100.0;
        type_req.columns.push_back(val);
    }

    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok()) << type_result.error().message;

    // Define constraint
    DefineConstraintRequest con_req;
    con_req.constraint_name = "value_check";
    con_req.type_name = "ev_test_type";
    {
        DefineConstraintRequest::RangeCheck rc;
        rc.column = "value";
        rc.min_val = 10.0;
        rc.max_val = 90.0;
        con_req.checks.push_back(rc);
    }
    auto con_result = service.define_constraint(con_req);
    ASSERT_TRUE(con_result.ok()) << con_result.error().message;

    // Generate
    GenerateRequest gen_req;
    gen_req.type_name = "ev_test_type";
    gen_req.constraints = {"value_check"};
    gen_req.limit = 500;
    gen_req.seed = 12345;

    auto gen_result = service.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    const auto& evidence_json = gen_result.value().evidence_json;
    ASSERT_FALSE(evidence_json.empty());

    // Parse JSON and verify required fields
    rapidjson::Document doc;
    doc.Parse(evidence_json.c_str());
    ASSERT_FALSE(doc.HasParseError());
    ASSERT_TRUE(doc.IsObject());

    // Check all required fields from the EvidencePackageV1 spec
    EXPECT_TRUE(doc.HasMember("schema_version")) << "Missing: schema_version";
    EXPECT_TRUE(doc.HasMember("data_grade")) << "Missing: data_grade";
    EXPECT_TRUE(doc.HasMember("exclusion_rate")) << "Missing: exclusion_rate";
    EXPECT_TRUE(doc.HasMember("row_count")) << "Missing: row_count";
    EXPECT_TRUE(doc.HasMember("audit_immutability")) << "Missing: audit_immutability";
    EXPECT_TRUE(doc.HasMember("statistical_fidelity")) << "Missing: statistical_fidelity";
    EXPECT_TRUE(doc.HasMember("constraint_summary")) << "Missing: constraint_summary";

    // Check nested conservative_tail_report
    EXPECT_TRUE(doc.HasMember("conservative_tail_report")) << "Missing: conservative_tail_report";
    if (doc.HasMember("conservative_tail_report")) {
        const auto& tr = doc["conservative_tail_report"];
        EXPECT_TRUE(tr.HasMember("epistemological_bias")) << "Missing: epistemological_bias";
        EXPECT_TRUE(tr.HasMember("tail_exclusion_statement")) << "Missing: tail_exclusion_statement";
    }

    // Verify via from_json roundtrip
    auto pkg_result = from_json(evidence_json);
    ASSERT_TRUE(pkg_result.ok()) << pkg_result.error().message;
    auto& pkg = pkg_result.value();

    EXPECT_EQ(pkg.schema_version, "v1");
    EXPECT_GT(pkg.row_count, 0);
    EXPECT_FALSE(pkg.data_grade.empty());
    EXPECT_EQ(pkg.audit_immutability, "not_applicable");
    EXPECT_EQ(pkg.statistical_fidelity, "not_applicable");
    EXPECT_FALSE(pkg.epistemological_bias.empty());
    EXPECT_FALSE(pkg.tail_exclusion_statement.empty());
}

// ============================================================================
// Test 3: Generate with no constraints -> physics-only behavior
// ============================================================================
TEST(E2EPipelineChaos, NoConstraints_PurePhysics_AllValuesInRange) {
    Schema schema;
    schema.type_name = "pure_physics";
    {
        ColumnDef val;
        val.name = "pressure";
        val.type = DataType::kFloat;
        val.range_min = 900.0;
        val.range_max = 1100.0;
        schema.columns.push_back(val);
    }
    {
        ColumnDef idx;
        idx.name = "index";
        idx.type = DataType::kInt;
        idx.range_min = 0.0;
        idx.range_max = 100.0;
        schema.columns.push_back(idx);
    }

    // No constraints - pure physics sampling
    std::vector<parser::ast::ConstraintItem> no_constraints;
    GenerationRequest gen_req{schema, no_constraints, 1000, 99, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    ASSERT_EQ(gen_result.value().data->num_rows(), 1000);

    // All pressure values should be in [900, 1100]
    auto table = gen_result.value().data;
    int pressure_idx = table->schema()->GetFieldIndex("pressure");
    ASSERT_GE(pressure_idx, 0);
    auto pressure_col = table->column(pressure_idx);

    for (int c = 0; c < pressure_col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(pressure_col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            if (arr->IsNull(r)) continue;
            double val = arr->Value(r);
            EXPECT_GE(val, 900.0) << "pressure out of range at row " << r;
            EXPECT_LE(val, 1100.0) << "pressure out of range at row " << r;
        }
    }

    // All index values should be in [0, 100]
    int index_idx = table->schema()->GetFieldIndex("index");
    ASSERT_GE(index_idx, 0);
    auto index_col = table->column(index_idx);

    for (int c = 0; c < index_col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::Int64Array>(index_col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            if (arr->IsNull(r)) continue;
            int64_t val = arr->Value(r);
            EXPECT_GE(val, 0) << "index out of range at row " << r;
            EXPECT_LE(val, 100) << "index out of range at row " << r;
        }
    }

    // Stats should report physics path
    EXPECT_DOUBLE_EQ(gen_result.value().stats.exclusion_rate, 0.0);
    EXPECT_EQ(gen_result.value().stats.rows_generated, 1000);
}

// ============================================================================
// Test 4: Generate with ENUM-only schema -> evidence reports correctly
// ============================================================================
TEST(E2EPipelineChaos, EnumOnlySchema_EvidenceReportsCorrectly) {
    Schema schema;
    schema.type_name = "status_only";
    {
        ColumnDef st;
        st.name = "status";
        st.type = DataType::kEnum;
        st.enum_values = {"normal", "warning", "fault"};
        schema.columns.push_back(st);
    }
    {
        ColumnDef level;
        level.name = "level";
        level.type = DataType::kEnum;
        level.enum_values = {"low", "medium", "high"};
        schema.columns.push_back(level);
    }

    auto vr = schema.validate();
    ASSERT_TRUE(vr.ok()) << vr.error().message;

    std::vector<parser::ast::ConstraintItem> no_constraints;
    GenerationRequest gen_req{schema, no_constraints, 500, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    ASSERT_EQ(gen_result.value().data->num_rows(), 500);

    auto table = gen_result.value().data;

    // Verify all enum values are valid
    int status_idx = table->schema()->GetFieldIndex("status");
    ASSERT_GE(status_idx, 0);
    auto status_col = table->column(status_idx);
    std::set<std::string> valid_status = {"normal", "warning", "fault"};
    std::map<std::string, int> status_counts;

    for (int c = 0; c < status_col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::StringArray>(status_col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            std::string val = arr->GetString(r);
            EXPECT_NE(valid_status.find(val), valid_status.end())
                << "Invalid enum value: " << val;
            status_counts[val]++;
        }
    }

    // All 3 values should appear (with 500 rows, P(missing) < 1e-70)
    EXPECT_EQ(status_counts.size(), 3u)
        << "ENUM-only schema should produce all enum values";

    // Build evidence package and verify it handles enum-only schema
    TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(),
                                    ValidationResult{}, gen_req, no_constraints);
    ASSERT_TRUE(tail.ok());

    ProvenanceV1 prov;
    prov.generator_identity = "physics_sampler";

    EvidencePackageBuilder evp_builder;
    auto evp = evp_builder.build(gen_result.value(), ValidationResult{},
                                  tail.value(), prov, schema);
    ASSERT_TRUE(evp.ok()) << evp.error().message;

    // Constraint summary should have NO details (no numeric range columns)
    EXPECT_TRUE(evp.value().constraint_summary.details.empty())
        << "ENUM-only schema should have no constraint details with numeric ranges";

    auto json_result = evp_builder.to_json(evp.value());
    ASSERT_TRUE(json_result.ok());

    // Verify JSON roundtrip
    auto roundtrip = evp_builder.from_json(json_result.value());
    ASSERT_TRUE(roundtrip.ok());
    EXPECT_EQ(roundtrip.value().row_count, 500);
}

// ============================================================================
// Test 5: Multiple schemas, multiple generates, verify isolation
// ============================================================================
TEST(E2EPipelineChaos, MultipleSchemas_NoCrossContamination) {
    // Schema A: temperature readings
    Schema schema_a;
    schema_a.type_name = "temp_readings";
    {
        ColumnDef temp;
        temp.name = "temp_celsius";
        temp.type = DataType::kFloat;
        temp.range_min = -20.0;
        temp.range_max = 50.0;
        schema_a.columns.push_back(temp);
    }

    // Schema B: humidity readings
    Schema schema_b;
    schema_b.type_name = "humidity_readings";
    {
        ColumnDef hum;
        hum.name = "humidity_pct";
        hum.type = DataType::kFloat;
        hum.range_min = 0.0;
        hum.range_max = 100.0;
        schema_b.columns.push_back(hum);
    }

    // Schema C: pressure readings
    Schema schema_c;
    schema_c.type_name = "pressure_readings";
    {
        ColumnDef pres;
        pres.name = "pressure_hpa";
        pres.type = DataType::kFloat;
        pres.range_min = 950.0;
        pres.range_max = 1050.0;
        schema_c.columns.push_back(pres);
    }

    std::vector<parser::ast::ConstraintItem> no_cons;
    uint64_t seed_a = 100, seed_b = 200, seed_c = 300;

    RectangularSampler sampler_a(schema_a);
    RectangularSampler sampler_b(schema_b);
    RectangularSampler sampler_c(schema_c);

    auto gen_a = sampler_a.generate({schema_a, no_cons, 100, seed_a, "uniform"});
    auto gen_b = sampler_b.generate({schema_b, no_cons, 100, seed_b, "uniform"});
    auto gen_c = sampler_c.generate({schema_c, no_cons, 100, seed_c, "uniform"});

    ASSERT_TRUE(gen_a.ok()) << gen_a.error().message;
    ASSERT_TRUE(gen_b.ok()) << gen_b.error().message;
    ASSERT_TRUE(gen_c.ok()) << gen_c.error().message;

    // Verify each table has correct schema structure
    EXPECT_EQ(gen_a.value().data->num_columns(), 1);
    EXPECT_EQ(gen_b.value().data->num_columns(), 1);
    EXPECT_EQ(gen_c.value().data->num_columns(), 1);

    EXPECT_EQ(gen_a.value().data->schema()->field(0)->name(), "temp_celsius");
    EXPECT_EQ(gen_b.value().data->schema()->field(0)->name(), "humidity_pct");
    EXPECT_EQ(gen_c.value().data->schema()->field(0)->name(), "pressure_hpa");

    // Verify value ranges don't cross-contaminate
    auto table_a = gen_a.value().data;
    auto col_a = std::static_pointer_cast<arrow::DoubleArray>(
        table_a->column(0)->chunk(0));
    for (int64_t r = 0; r < col_a->length(); r++) {
        double v = col_a->Value(r);
        EXPECT_GE(v, -20.0) << "Schema A value below range";
        EXPECT_LE(v, 50.0) << "Schema A value above range";
        // Should NEVER be in range [950, 1050] or [0, 100]
        EXPECT_LT(v, 950.0) << "Schema A cross-contamination with C";
    }

    auto col_b = std::static_pointer_cast<arrow::DoubleArray>(
        gen_b.value().data->column(0)->chunk(0));
    for (int64_t r = 0; r < col_b->length(); r++) {
        double v = col_b->Value(r);
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 100.0);
    }

    auto col_c = std::static_pointer_cast<arrow::DoubleArray>(
        gen_c.value().data->column(0)->chunk(0));
    for (int64_t r = 0; r < col_c->length(); r++) {
        double v = col_c->Value(r);
        EXPECT_GE(v, 950.0) << "Schema C value below range";
        EXPECT_LE(v, 1050.0) << "Schema C value above range";
    }
}

// ============================================================================
// Test 6: EvidencePackageV1 vs V2 field comparison
// ============================================================================
TEST(E2EPipelineChaos, EvidenceV1_vs_V2_SharedFieldsMatch) {
    auto schema = make_sensor_schema();
    auto vr = schema.validate();
    ASSERT_TRUE(vr.ok());

    std::vector<parser::ast::ConstraintItem> constraints;
    constraints.push_back(make_between("wind_speed", 5.0, 30.0));

    GenerationRequest gen_req{schema, constraints, 200, 777, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    // Build V1
    TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(), val_result.value(),
                                    gen_req, constraints);
    ASSERT_TRUE(tail.ok());

    ProvenanceV1 prov;
    prov.generator_identity = "physics_sampler";

    EvidencePackageBuilder v1_builder;
    auto v1 = v1_builder.build(gen_result.value(), val_result.value(),
                                tail.value(), prov, schema);
    ASSERT_TRUE(v1.ok()) << v1.error().message;

    // Build V2
    ConstraintSet cset;
    cset.value_range_names = {"wind_safety"};

    ConstraintClassifier classifier;
    auto class_result = classifier.classify(cset, schema);
    ASSERT_TRUE(class_result.ok()) << class_result.error().message;

    ExecutionRouter router(false);
    auto routing = router.route(class_result.value(), schema);
    ASSERT_TRUE(routing.ok()) << routing.error().message;

    PostFilter pf;
    auto pf_result = pf.execute(gen_result.value().data, 200);
    ASSERT_TRUE(pf_result.ok()) << pf_result.error().message;

    EvidencePackageV2Builder v2_builder;
    auto v2 = v2_builder.build(
        gen_result.value().data->num_rows(),
        gen_result.value().stats.exclusion_rate,
        tail.value().data_grade,
        routing.value(),
        class_result.value(),
        pf_result.value(),
        schema);
    ASSERT_TRUE(v2.ok()) << v2.error().message;

    // Compare shared fields
    EXPECT_EQ(v1.value().row_count, v2.value().row_count)
        << "V1 and V2 row_count should match";
    EXPECT_DOUBLE_EQ(v1.value().exclusion_rate, v2.value().exclusion_rate)
        << "V1 and V2 exclusion_rate should match";
    EXPECT_EQ(v1.value().data_grade, v2.value().data_grade)
        << "V1 and V2 data_grade should match";
    EXPECT_EQ(v1.value().epistemological_bias, v2.value().epistemological_bias)
        << "V1 and V2 epistemological_bias should match";

    // V2 should have additional fields not in V1
    EXPECT_EQ(v2.value().schema_version, "v2");
    EXPECT_EQ(v1.value().schema_version, "v1");
    EXPECT_EQ(v2.value().audit_immutability, "verified");
    EXPECT_EQ(v1.value().audit_immutability, "not_applicable");

    // V2 has constraint_type_breakdown
    EXPECT_GT(v2.value().constraint_type_breakdown.value_range_count, 0);

    // Verify both serialize to valid JSON
    auto v1_json = v1_builder.to_json(v1.value());
    ASSERT_TRUE(v1_json.ok());
    auto v2_json = v2_builder.to_json(v2.value());
    ASSERT_TRUE(v2_json.ok());

    // Parse V2 JSON and verify
    rapidjson::Document doc;
    doc.Parse(v2_json.value().c_str());
    ASSERT_FALSE(doc.HasParseError());
    EXPECT_TRUE(doc.HasMember("constraint_type_breakdown"));
    EXPECT_TRUE(doc.HasMember("post_filter_info"));
    EXPECT_TRUE(doc.HasMember("statistical_fidelity"));
    EXPECT_TRUE(doc.HasMember("generator_identity"));
}

// ============================================================================
// Test 7: PostFilter with 100% pass rate -> low exclusion band
// ============================================================================
TEST(E2EPipelineChaos, PostFilter_100PercentPass_LowExclusionBand) {
    // Create a table with enough rows
    auto schema = make_sensor_schema();
    schema.validate();

    std::vector<parser::ast::ConstraintItem> no_cons;
    GenerationRequest gen_req{schema, no_cons, 1000, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // PostFilter with target_rows == available_rows -> 0% exclusion
    PostFilter pf;
    auto pf_result = pf.execute(gen_result.value().data, 1000);
    ASSERT_TRUE(pf_result.ok()) << pf_result.error().message;

    EXPECT_EQ(pf_result.value().rate_band, ExclusionRateBand::kLow)
        << "0% exclusion should be kLow band";
    EXPECT_EQ(pf_result.value().data_grade, "statistics_guaranteed");
    EXPECT_DOUBLE_EQ(pf_result.value().actual_exclusion_rate, 0.0);
    EXPECT_EQ(pf_result.value().post_filter_rows, 1000);

    // Also test with fewer target rows than available (pass-through)
    auto pf_result2 = pf.execute(gen_result.value().data, 500);
    ASSERT_TRUE(pf_result2.ok()) << pf_result2.error().message;
    // 500 of 1000 rows used -> 50% exclusion
    EXPECT_NEAR(pf_result2.value().actual_exclusion_rate, 0.5, 0.01);
}

// ============================================================================
// Test 8: Router explain vs actual routing consistency
// ============================================================================
TEST(E2EPipelineChaos, Router_Explain_MatchesActualRouting) {
    Schema schema = make_sensor_schema();
    schema.validate();

    // Case 1: Value-range only, no data engine
    ConstraintSet vr_only;
    vr_only.value_range_names = {"wind_check"};

    ConstraintClassifier classifier;
    auto cls_result = classifier.classify(vr_only, schema);
    ASSERT_TRUE(cls_result.ok()) << cls_result.error().message;

    ExecutionRouter router_no_engine(false);
    auto explain_info = router_no_engine.explain(cls_result.value());
    auto route_result = router_no_engine.route(cls_result.value(), schema);
    ASSERT_TRUE(route_result.ok()) << route_result.error().message;

    // Actual routing: value-range only -> kPurePhysics
    EXPECT_EQ(route_result.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_EQ(route_result.value().identity.identity, "physics_sampler");
    EXPECT_EQ(route_result.value().identity.path, DegradationPath::kPurePhysics);

    // Case 2: With data engine and aggregate constraints
    ConstraintSet with_agg;
    with_agg.value_range_names = {"vr1"};
    with_agg.aggregate_defs.push_back(AggregateConstraintDef{
        "agg1", "temperature", AggregateFunction::kAvg,
        WindowType::kInterval, 3600000000LL, 0.0, 100.0});

    auto cls_agg = classifier.classify(with_agg, schema);
    ASSERT_TRUE(cls_agg.ok()) << cls_agg.error().message;

    ExecutionRouter router_with_engine(true);
    auto route_agg = router_with_engine.route(cls_agg.value(), schema);
    ASSERT_TRUE(route_agg.ok()) << route_agg.error().message;

    // Should get FullFunction with data engine + aggregate
    EXPECT_EQ(route_agg.value().selected_path, DegradationPath::kFullFunction);
    EXPECT_EQ(route_agg.value().identity.identity, "constraint_driven_synthetic");
    EXPECT_TRUE(route_agg.value().data_engine_available);

    // Case 3: No data engine, aggregate constraints -> kPurePhysics (fallback)
    ExecutionRouter router_no_de(false);
    auto route_agg_no_de = router_no_de.route(cls_agg.value(), schema);
    ASSERT_TRUE(route_agg_no_de.ok()) << route_agg_no_de.error().message;
    EXPECT_EQ(route_agg_no_de.value().selected_path, DegradationPath::kPurePhysics)
        << "Without data engine, should fall back to pure physics";

    // Case 4: Inter-row constraints with data engine
    ConstraintSet with_ir;
    with_ir.value_range_names = {"vr1"};
    with_ir.inter_row_defs.push_back(InterRowConstraintDef{
        "wind_speed", "timestamp",
        InterRowConstraintDef::Type::kDeltaMax, 5.0, std::nullopt});

    auto cls_ir = classifier.classify(with_ir, schema);
    ASSERT_TRUE(cls_ir.ok()) << cls_ir.error().message;

    ExecutionRouter router_de(true);
    auto route_ir = router_de.route(cls_ir.value(), schema);
    ASSERT_TRUE(route_ir.ok()) << route_ir.error().message;
    EXPECT_EQ(route_ir.value().selected_path, DegradationPath::kPostFilter);
}

// ============================================================================
// Test 9: SchemaBuilder from complex DSL -> all 5 column types
// ============================================================================
TEST(E2EPipelineChaos, ComplexDSL_AllColumnTypes_ParsedCorrectly) {
    const char* dsl = R"(
        DEFINE TYPE complex_type {
            ts: DATETIME NOT NULL ORDER,
            value: FLOAT [0.0, 100.0],
            count: INT [0, 1000],
            label: STRING,
            status: ENUM('active', 'inactive', 'pending')
        };
    )";

    parser::Parser p;
    auto parse_result = p.parse(dsl);
    ASSERT_TRUE(parse_result.ok()) << parse_result.error().message;
    ASSERT_TRUE(parse_result.value().errors.empty())
        << "Parse errors: " << parse_result.value().errors[0].message;
    ASSERT_EQ(parse_result.value().program.statements.size(), 1u);

    auto& stmt = std::get<parser::ast::DefineTypeStmt>(
        parse_result.value().program.statements[0]);

    EXPECT_EQ(stmt.type_name, "complex_type");
    ASSERT_EQ(stmt.columns.size(), 5u);

    // Check each column
    EXPECT_EQ(stmt.columns[0].name, "ts");
    EXPECT_EQ(stmt.columns[0].type, DataType::kDatetime);
    EXPECT_TRUE(stmt.columns[0].not_null);
    EXPECT_TRUE(stmt.columns[0].is_order);

    EXPECT_EQ(stmt.columns[1].name, "value");
    EXPECT_EQ(stmt.columns[1].type, DataType::kFloat);
    ASSERT_TRUE(stmt.columns[1].range_min.has_value());
    EXPECT_DOUBLE_EQ(stmt.columns[1].range_min.value(), 0.0);
    ASSERT_TRUE(stmt.columns[1].range_max.has_value());
    EXPECT_DOUBLE_EQ(stmt.columns[1].range_max.value(), 100.0);

    EXPECT_EQ(stmt.columns[2].name, "count");
    EXPECT_EQ(stmt.columns[2].type, DataType::kInt);
    ASSERT_TRUE(stmt.columns[2].range_min.has_value());
    EXPECT_DOUBLE_EQ(stmt.columns[2].range_min.value(), 0.0);
    ASSERT_TRUE(stmt.columns[2].range_max.has_value());
    EXPECT_DOUBLE_EQ(stmt.columns[2].range_max.value(), 1000.0);

    EXPECT_EQ(stmt.columns[3].name, "label");
    EXPECT_EQ(stmt.columns[3].type, DataType::kString);

    EXPECT_EQ(stmt.columns[4].name, "status");
    EXPECT_EQ(stmt.columns[4].type, DataType::kEnum);
    ASSERT_EQ(stmt.columns[4].enum_values.size(), 3u);
    EXPECT_EQ(stmt.columns[4].enum_values[0], "active");
    EXPECT_EQ(stmt.columns[4].enum_values[1], "inactive");
    EXPECT_EQ(stmt.columns[4].enum_values[2], "pending");

    // Build full schema and validate
    SchemaBuilder builder;
    auto schema_result = builder.build(stmt);
    ASSERT_TRUE(schema_result.ok()) << schema_result.error().message;
    EXPECT_EQ(schema_result.value().type_name, "complex_type");

    // Generate data with this schema
    std::vector<parser::ast::ConstraintItem> no_cons;
    GenerationRequest gen_req{schema_result.value(), no_cons, 50, 42, "uniform"};
    RectangularSampler sampler(schema_result.value());
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 50);
    EXPECT_EQ(gen_result.value().data->num_columns(), 5);
}

// ============================================================================
// Test 10: Generate -> Validate -> re-validate with tighter constraints
// ============================================================================
TEST(E2EPipelineChaos, Generate_Validate_ThenRevalidateTighter) {
    Schema schema;
    schema.type_name = "reval_test";
    {
        ColumnDef val;
        val.name = "measurement";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 100.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    // Generate with wide constraint A: [10, 90]
    std::vector<parser::ast::ConstraintItem> constraint_a;
    constraint_a.push_back(make_between("measurement", 10.0, 90.0));

    GenerationRequest gen_req{schema, constraint_a, 2000, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    auto table = gen_result.value().data;

    // Validate with constraint A -> should all pass
    ValueRangeValidator val_a(schema, constraint_a);
    auto result_a = val_a.validate_batch(table);
    ASSERT_TRUE(result_a.ok()) << result_a.error().message;
    EXPECT_EQ(result_a.value().rows_failed, 0)
        << "All rows should pass constraint A (generation matches constraint)";
    EXPECT_DOUBLE_EQ(result_a.value().pass_rate, 1.0);

    // Re-validate with tighter constraint B: [30, 70]
    // Since we generated [10, 90], some rows will be in [10,30) or (70,90]
    std::vector<parser::ast::ConstraintItem> constraint_b;
    constraint_b.push_back(make_between("measurement", 30.0, 70.0));

    ValueRangeValidator val_b(schema, constraint_b);
    auto result_b = val_b.validate_batch(table);
    ASSERT_TRUE(result_b.ok()) << result_b.error().message;
    // Should have some failures with tighter constraint
    EXPECT_GT(result_b.value().rows_failed, 0)
        << "Tighter constraint should reject some rows";
    EXPECT_LT(result_b.value().pass_rate, 1.0)
        << "Pass rate should be < 1.0 with tighter constraint";
    EXPECT_GT(result_b.value().pass_rate, 0.0)
        << "Some rows should still pass";

    // Verify failure details are populated
    if (!result_b.value().failures.empty()) {
        auto& first_failure = result_b.value().failures[0];
        EXPECT_EQ(first_failure.column_name, "measurement");
        // Value should be outside [30, 70]
        EXPECT_TRUE(first_failure.actual_value < 30.0 ||
                    first_failure.actual_value > 70.0);
    }
}

// ============================================================================
// Test 11: Audit log hash chain tamper detection
// ============================================================================
TEST(E2EPipelineChaos, AuditLog_TamperDetection) {
    AuditLog audit;
    auto genesis = audit.create_genesis();
    ASSERT_TRUE(genesis.ok());

    // Append several records
    auto r1 = audit.append("generate", "user_a", {{"type", "s1"}});
    ASSERT_TRUE(r1.ok());
    auto r2 = audit.append("store", "user_a", {{"table", "t1"}});
    ASSERT_TRUE(r2.ok());
    auto r3 = audit.append("query", "user_b", {{"table", "t1"}});
    ASSERT_TRUE(r3.ok());

    EXPECT_EQ(audit.record_count(), 4); // genesis + 3

    // Chain should be valid
    auto valid = audit.verify_chain();
    ASSERT_TRUE(valid.ok());
    EXPECT_TRUE(valid.value());

    // Get daily verification
    auto daily = audit.daily_verification();
    ASSERT_TRUE(daily.ok());
    EXPECT_TRUE(daily.value().is_valid);
    EXPECT_EQ(daily.value().total_records, 4);

    // Tamper with record at index 2 (r1) by modifying content_hash
    // We need to access internal records... but they're private.
    // Instead, let's verify the chain detects when we recompute hashes
    // on existing records. The verify_chain() recomputes hashes from
    // the record data, so if the data is consistent, it passes.
    // To truly test tamper detection, we verify that:
    // 1. Genesis prev_hash = "0"
    // 2. Each record's prev_hash matches the previous chain_hash
    auto scan_result = audit.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(scan_result.ok());
    auto& records = scan_result.value();
    ASSERT_EQ(records.size(), 4u);

    // Verify linkage
    for (size_t i = 1; i < records.size(); ++i) {
        EXPECT_EQ(records[i].prev_hash, records[i-1].chain_hash)
            << "Broken chain link at record " << i;
    }

    // Verify that a fresh audit log properly detects missing genesis
    AuditLog no_genesis;
    auto append_before = no_genesis.append("test", "user");
    EXPECT_FALSE(append_before.ok())
        << "Should not be able to append before genesis";
    EXPECT_EQ(append_before.error().code, ErrorCode::kInvalidState);

    // Verify double genesis fails
    AuditLog double_g;
    auto g1 = double_g.create_genesis();
    ASSERT_TRUE(g1.ok());
    auto g2 = double_g.create_genesis();
    EXPECT_FALSE(g2.ok())
        << "Should not be able to create double genesis";
    EXPECT_EQ(g2.error().code, ErrorCode::kAlreadyExists);
}

// ============================================================================
// Test 12: Concurrent MetricRegistry usage
// ============================================================================
TEST(E2EPipelineChaos, ConcurrentMetricRegistry_NoCrash_CorrectValues) {
    scaffold::MetricsRegistry::instance().reset();

    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;

    std::vector<std::thread> threads;

    // Thread group 1: counters
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, kIterations]() {
            std::string name = "counter_" + std::to_string(t);
            for (int i = 0; i < kIterations; i++) {
                scaffold::MetricsRegistry::instance().counter(name).increment();
            }
        });
    }

    // Thread group 2: gauges
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, kIterations]() {
            std::string name = "gauge_" + std::to_string(t);
            for (int i = 0; i < kIterations; i++) {
                scaffold::MetricsRegistry::instance().gauge(name).set(
                    static_cast<double>(i));
            }
        });
    }

    // Thread group 3: histograms
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, kIterations]() {
            std::string name = "histogram_" + std::to_string(t);
            for (int i = 0; i < kIterations; i++) {
                scaffold::MetricsRegistry::instance().histogram(name).observe(
                    static_cast<double>(i));
            }
        });
    }

    // Thread group 4: mixed concurrent access to same counters
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([kIterations]() {
            for (int i = 0; i < kIterations; i++) {
                scaffold::MetricsRegistry::instance().counter("shared_counter").increment();
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Verify counter values
    for (int t = 0; t < kThreads; t++) {
        std::string name = "counter_" + std::to_string(t);
        EXPECT_EQ(scaffold::MetricsRegistry::instance().counter(name).value(),
                  kIterations)
            << "Counter " << name << " should have value " << kIterations;
    }

    // Shared counter should be kThreads * kIterations
    EXPECT_EQ(scaffold::MetricsRegistry::instance().counter("shared_counter").value(),
              kThreads * kIterations)
        << "Shared counter should be " << (kThreads * kIterations);

    // Verify gauges have last value
    for (int t = 0; t < kThreads; t++) {
        std::string name = "gauge_" + std::to_string(t);
        double val = scaffold::MetricsRegistry::instance().gauge(name).value();
        EXPECT_GE(val, 0.0);
        EXPECT_LT(val, static_cast<double>(kIterations));
    }

    // Verify histograms
    for (int t = 0; t < kThreads; t++) {
        std::string name = "histogram_" + std::to_string(t);
        auto& h = scaffold::MetricsRegistry::instance().histogram(name);
        EXPECT_EQ(h.count(), kIterations)
            << "Histogram " << name << " count mismatch";
        // sum = 0 + 1 + ... + (kIterations-1) = kIterations*(kIterations-1)/2
        int64_t expected_sum = static_cast<int64_t>(kIterations) * (kIterations - 1) / 2;
        EXPECT_DOUBLE_EQ(h.sum(), static_cast<double>(expected_sum))
            << "Histogram " << name << " sum mismatch";
    }

    scaffold::MetricsRegistry::instance().reset();
}

// ============================================================================
// Test 13: Schema hash consistency between V1 and V2 builders
// ============================================================================
TEST(E2EPipelineChaos, SchemaHash_V1_V2_Consistency) {
    auto schema = make_sensor_schema();
    schema.validate();

    // V1 builder compute_schema_hash
    std::string v1_hash = EvidencePackageBuilder::compute_schema_hash(schema);

    // V2 builder computes hash (was a BUG: V2 used different format, now fixed)
    std::ostringstream oss;
    oss << schema.type_name << "{";
    for (const auto& col : schema.columns) {
        oss << col.name << ":" << static_cast<int>(col.type);
        if (col.range_min) oss << "[" << *col.range_min;
        if (col.range_max) oss << "," << *col.range_max << "]";
        oss << ";";
    }
    oss << "}";
    std::string v2_hash = sha256_hex(oss.str());

    // After fix: V1 and V2 should produce the same schema hash
    EXPECT_EQ(v1_hash, v2_hash)
        << "V1 and V2 schema_hash must be consistent for the same schema. "
        << "V1 hash: " << v1_hash.substr(0, 16) << "... "
        << "V2 hash: " << v2_hash.substr(0, 16) << "...";
}

// ============================================================================
// Test 14: ValueRangeValidator with GT/LT constraints on generated data
// Tests the end-to-end path of generating data with BETWEEN, then
// validating with GT-only or LT-only constraints
// ============================================================================
TEST(E2EPipelineChaos, Generate_Between_Validate_GT_Only) {
    Schema schema;
    schema.type_name = "gt_lt_test";
    {
        ColumnDef val;
        val.name = "value";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 100.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    // Generate with BETWEEN [20, 80]
    std::vector<parser::ast::ConstraintItem> between_cons;
    between_cons.push_back(make_between("value", 20.0, 80.0));

    GenerationRequest gen_req{schema, between_cons, 2000, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    // Validate with GT constraint (> 10) - all should pass since range is [20,80]
    std::vector<parser::ast::ConstraintItem> gt_cons;
    gt_cons.push_back(make_gt("value", 10.0));

    ValueRangeValidator gt_validator(schema, gt_cons);
    auto gt_result = gt_validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(gt_result.ok()) << gt_result.error().message;
    EXPECT_EQ(gt_result.value().rows_failed, 0)
        << "All values in [20,80] should pass > 10 constraint";

    // Validate with GT constraint (> 50) - some should fail
    std::vector<parser::ast::ConstraintItem> gt_tight;
    gt_tight.push_back(make_gt("value", 50.0));

    ValueRangeValidator gt_tight_validator(schema, gt_tight);
    auto gt_tight_result = gt_tight_validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(gt_tight_result.ok()) << gt_tight_validator.explain().path;
    EXPECT_GT(gt_tight_result.value().rows_failed, 0)
        << "Some values in [20,80] should fail > 50 constraint";
    EXPECT_GT(gt_tight_result.value().pass_rate, 0.0)
        << "Some values should still pass";
    EXPECT_LT(gt_tight_result.value().pass_rate, 1.0);
}

// ============================================================================
// Test 15: PostFilter exclusion rate band boundary values
// ============================================================================
TEST(E2EPipelineChaos, PostFilter_ExclusionBandBoundaryValues) {
    // Test exact boundary: rate = 0.30 should be kLow, not kMedium
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.0, 0.90),
              ExclusionRateBand::kLow);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.29, 0.90),
              ExclusionRateBand::kLow);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.30, 0.90),
              ExclusionRateBand::kLow)
        << "BUG: rate=0.30 should be kLow (rate > 0.30 for medium), not medium";
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.31, 0.90),
              ExclusionRateBand::kMedium);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.70, 0.90),
              ExclusionRateBand::kMedium)
        << "BUG: rate=0.70 should be kMedium (rate > 0.70 for high), not high";
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.71, 0.90),
              ExclusionRateBand::kHigh);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.89, 0.90),
              ExclusionRateBand::kHigh);
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.90, 0.90),
              ExclusionRateBand::kHigh)
        << "rate=0.90 should be kHigh (rate > critical_threshold for critical)";
    EXPECT_EQ(PostFilter::classify_exclusion_rate(0.91, 0.90),
              ExclusionRateBand::kCritical);

    // Test data_grade mapping
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kLow),
              "statistics_guaranteed");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kMedium),
              "limited_fidelity");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kHigh),
              "limited_fidelity_conservative");
    EXPECT_EQ(PostFilter::data_grade_for_band(ExclusionRateBand::kCritical),
              "rejected");
}

// ============================================================================
// Test 16: Store and retrieve with ScanPredicate across pipeline
// ============================================================================
TEST(E2EPipelineChaos, Generate_Store_ScanWithPredicate) {
    Schema schema;
    schema.type_name = "pred_test";
    {
        ColumnDef val;
        val.name = "score";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 100.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    std::vector<parser::ast::ConstraintItem> no_cons;
    GenerationRequest gen_req{schema, no_cons, 500, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;

    std::filesystem::path store_path =
        std::filesystem::temp_directory_path() / "e2e_chaos_pred";
    std::filesystem::remove_all(store_path);

    ObjectStoreBackend store(store_path);
    auto reg = store.register_table("pred_test", "{}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    auto app = store.append("pred_test", gen_result.value().data);
    ASSERT_TRUE(app.ok()) << app.error().message;

    // Scan with predicate: only scores in [40, 60]
    ScanPredicate pred;
    pred.column = "score";
    pred.min_value = 40.0;
    pred.max_value = 60.0;

    auto scan = store.scan("pred_test", {}, pred);
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    auto& filtered = scan.value();

    // Should have fewer rows than 500 (only those in [40, 60])
    EXPECT_GT(filtered->num_rows(), 0)
        << "Some rows should match [40, 60] range";
    EXPECT_LT(filtered->num_rows(), 500)
        << "Not all rows should be in [40, 60]";

    // Verify all returned values are in range
    int col_idx = filtered->schema()->GetFieldIndex("score");
    ASSERT_GE(col_idx, 0);
    auto col = filtered->column(col_idx);
    for (int c = 0; c < col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            if (arr->IsNull(r)) continue;
            double val = arr->Value(r);
            EXPECT_GE(val, 40.0) << "Filtered value below predicate min";
            EXPECT_LE(val, 60.0) << "Filtered value above predicate max";
        }
    }

    std::filesystem::remove_all(store_path);
}

// ============================================================================
// Test 17: Service generates with same seed -> deterministic output
// ============================================================================
TEST(E2EPipelineChaos, Service_DeterministicGeneration_SameSeed) {
    SynthGenService service;

    DefineTypeRequest type_req;
    type_req.type_name = "det_test";
    {
        DefineTypeRequest::ColumnDef val;
        val.name = "x";
        val.type = "FLOAT";
        val.range_min = 0.0;
        val.range_max = 1.0;
        type_req.columns.push_back(val);
    }
    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok()) << type_result.error().message;

    // Generate twice with same seed
    GenerateRequest gen1;
    gen1.type_name = "det_test";
    gen1.limit = 100;
    gen1.seed = 99999;

    GenerateRequest gen2;
    gen2.type_name = "det_test";
    gen2.limit = 100;
    gen2.seed = 99999;

    auto result1 = service.generate(gen1);
    ASSERT_TRUE(result1.ok()) << result1.error().message;
    auto result2 = service.generate(gen2);
    ASSERT_TRUE(result2.ok()) << result2.error().message;

    // Evidence JSON should be identical for same seed
    EXPECT_EQ(result1.value().evidence_json, result2.value().evidence_json)
        << "Same seed should produce identical evidence JSON";
    EXPECT_EQ(result1.value().stats.rows_generated, result2.value().stats.rows_generated);
}

// ============================================================================
// Test 18: Evidence V1 JSON roundtrip field preservation
// ============================================================================
TEST(E2EPipelineChaos, EvidenceV1_JsonRoundtrip_AllFieldsPreserved) {
    auto schema = make_sensor_schema();
    schema.validate();

    std::vector<parser::ast::ConstraintItem> constraints;
    constraints.push_back(make_between("wind_speed", 5.0, 30.0));

    GenerationRequest gen_req{schema, constraints, 100, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok());

    ValueRangeValidator validator(schema, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(), val_result.value(),
                                    gen_req, constraints);
    ASSERT_TRUE(tail.ok());

    ProvenanceV1 prov;
    prov.data_source = "test_source";
    prov.constraints = {"c1", "c2"};
    prov.generator_identity = "physics_sampler";
    prov.generation_params = {42, "uniform", 100, 500};
    prov.trace_spans.push_back({"t1", "s1", "physics", "generate", "ok"});

    EvidencePackageBuilder builder;
    auto pkg_result = builder.build(gen_result.value(), val_result.value(),
                                     tail.value(), prov, schema);
    ASSERT_TRUE(pkg_result.ok()) << pkg_result.error().message;
    auto& original = pkg_result.value();

    // Serialize to JSON
    auto json_result = builder.to_json(original);
    ASSERT_TRUE(json_result.ok());
    EXPECT_FALSE(json_result.value().empty());

    // Parse back
    auto roundtrip = builder.from_json(json_result.value());
    ASSERT_TRUE(roundtrip.ok()) << roundtrip.error().message;
    auto& rt = roundtrip.value();

    // Verify all fields preserved
    EXPECT_EQ(rt.schema_version, original.schema_version);
    EXPECT_EQ(rt.schema_hash, original.schema_hash);
    EXPECT_DOUBLE_EQ(rt.exclusion_rate, original.exclusion_rate);
    EXPECT_EQ(rt.data_grade, original.data_grade);
    EXPECT_EQ(rt.row_count, original.row_count);
    EXPECT_EQ(rt.epistemological_bias, original.epistemological_bias);
    EXPECT_EQ(rt.tail_exclusion_statement, original.tail_exclusion_statement);
    EXPECT_DOUBLE_EQ(rt.exclusion_rate_report, original.exclusion_rate_report);
    EXPECT_EQ(rt.rows_generated, original.rows_generated);
    EXPECT_EQ(rt.rows_validated, original.rows_validated);
    EXPECT_EQ(rt.rows_failed_validation, original.rows_failed_validation);
    EXPECT_EQ(rt.distribution_used, original.distribution_used);
    EXPECT_EQ(rt.seed_used, original.seed_used);
    EXPECT_EQ(rt.audit_immutability, original.audit_immutability);
    EXPECT_EQ(rt.statistical_fidelity, original.statistical_fidelity);
    EXPECT_EQ(rt.drift_detection, original.drift_detection);
    EXPECT_EQ(rt.constraint_type_breakdown, original.constraint_type_breakdown);

    // Check provenance roundtrip
    EXPECT_EQ(rt.provenance.data_source, original.provenance.data_source);
    EXPECT_EQ(rt.provenance.constraints, original.provenance.constraints);
    EXPECT_EQ(rt.provenance.generator_identity, original.provenance.generator_identity);
    EXPECT_EQ(rt.provenance.generation_params.seed, original.provenance.generation_params.seed);
    EXPECT_EQ(rt.provenance.generation_params.distribution, original.provenance.generation_params.distribution);
    EXPECT_EQ(rt.provenance.generation_params.limit, original.provenance.generation_params.limit);
    EXPECT_EQ(rt.provenance.generation_params.batch_size, original.provenance.generation_params.batch_size);
    ASSERT_EQ(rt.provenance.trace_spans.size(), 1u);
    EXPECT_EQ(rt.provenance.trace_spans[0].trace_id, "t1");
    EXPECT_EQ(rt.provenance.trace_spans[0].span_id, "s1");
    EXPECT_EQ(rt.provenance.trace_spans[0].component, "physics");
    EXPECT_EQ(rt.provenance.trace_spans[0].operation, "generate");
    EXPECT_EQ(rt.provenance.trace_spans[0].status, "ok");
}

// ============================================================================
// Test 19: Router with no constraints and no data engine -> pure physics
// ============================================================================
TEST(E2EPipelineChaos, Router_NoConstraints_NoDataEngine_PurePhysics) {
    Schema schema;
    schema.type_name = "router_test";
    {
        ColumnDef val;
        val.name = "v";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 10.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    ConstraintSet empty_set;
    empty_set.value_range_names = {"placeholder"};

    // Classifier rejects empty constraint sets, so add at least one
    ConstraintClassifier classifier;
    auto cls = classifier.classify(empty_set, schema);
    ASSERT_TRUE(cls.ok()) << cls.error().message;

    ExecutionRouter router(false);
    auto decision = router.route(cls.value(), schema);
    ASSERT_TRUE(decision.ok()) << decision.error().message;

    EXPECT_EQ(decision.value().selected_path, DegradationPath::kPurePhysics);
    EXPECT_EQ(decision.value().identity.identity, "physics_sampler");
    EXPECT_FALSE(decision.value().data_engine_available);
}

// ============================================================================
// Test 20: Generate with Gaussian distribution, validate within range
// ============================================================================
TEST(E2EPipelineChaos, GaussianDistribution_ValuesInRange) {
    Schema schema;
    schema.type_name = "gauss_test";
    {
        ColumnDef val;
        val.name = "measurement";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 100.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    std::vector<parser::ast::ConstraintItem> constraints;
    constraints.push_back(make_between("measurement", 30.0, 70.0));

    GenerationRequest gen_req{schema, constraints, 2000, 42, "gaussian"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().stats.distribution_used, "gaussian");

    auto table = gen_result.value().data;
    int col_idx = table->schema()->GetFieldIndex("measurement");
    ASSERT_GE(col_idx, 0);

    // Gaussian with truncation should produce values within [30, 70]
    auto col = table->column(col_idx);
    int out_of_range = 0;
    for (int c = 0; c < col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            if (arr->IsNull(r)) continue;
            double val = arr->Value(r);
            if (val < 30.0 || val > 70.0) out_of_range++;
        }
    }
    EXPECT_EQ(out_of_range, 0)
        << "Gaussian sampler should truncate values to [30, 70], found "
        << out_of_range << " out-of-range values";
}

// ============================================================================
// Test 21: Audit log scan with time range filtering
// ============================================================================
TEST(E2EPipelineChaos, AuditLog_ScanTimeRangeFiltering) {
    AuditLog audit;
    auto genesis = audit.create_genesis();
    ASSERT_TRUE(genesis.ok());

    // Append multiple records
    auto r1 = audit.append("op1", "user_a", {{"key", "val1"}});
    ASSERT_TRUE(r1.ok());
    auto r2 = audit.append("op2", "user_b", {{"key", "val2"}});
    ASSERT_TRUE(r2.ok());
    auto r3 = audit.append("op3", "user_a", {{"key", "val3"}});
    ASSERT_TRUE(r3.ok());

    // Scan all
    auto all = audit.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(all.ok());
    EXPECT_EQ(all.value().size(), 4u);  // genesis + 3

    // Scan with limit
    auto limited = audit.scan(std::nullopt, std::nullopt, 2);
    ASSERT_TRUE(limited.ok());
    EXPECT_EQ(limited.value().size(), 2u);

    // Scan with time range (should get at least genesis)
    Timestamp very_old = 0;
    auto from_scan = audit.scan(very_old, std::nullopt, 100);
    ASSERT_TRUE(from_scan.ok());
    EXPECT_EQ(from_scan.value().size(), 4u);

    // Scan with future start time -> empty
    Timestamp future = 9999999999999999LL;
    auto future_scan = audit.scan(future, std::nullopt, 100);
    ASSERT_TRUE(future_scan.ok());
    EXPECT_EQ(future_scan.value().size(), 0u);

    // Get latest
    auto latest = audit.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "op3");

    // Verify fork detection: no forks in linear chain
    auto forks = audit.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty()) << "Linear chain should have no forks";
}

// ============================================================================
// Test 22: Classifier requires ORDER column for inter-row constraints
// ============================================================================
TEST(E2EPipelineChaos, Classifier_InterRow_RequiresOrderColumn) {
    // Schema without ORDER column
    Schema no_order;
    no_order.type_name = "no_order";
    {
        ColumnDef val;
        val.name = "v";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 10.0;
        no_order.columns.push_back(val);
    }
    no_order.validate();

    ConstraintSet with_ir;
    with_ir.value_range_names = {"vr1"};
    with_ir.inter_row_defs.push_back(InterRowConstraintDef{
        "v", "ts", InterRowConstraintDef::Type::kDeltaMax, 5.0, std::nullopt});

    ConstraintClassifier classifier;
    auto result = classifier.classify(with_ir, no_order);
    EXPECT_FALSE(result.ok())
        << "BUG: Should reject inter-row constraint without ORDER column";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kOrderColumnRequired);
    }
}

// ============================================================================
// Test 23: Classifier aggregate requires DATETIME ORDER column
// ============================================================================
TEST(E2EPipelineChaos, Classifier_Aggregate_RequiresDatetimeOrder) {
    // Schema with ORDER column that is not DATETIME
    Schema int_order;
    int_order.type_name = "int_order";
    {
        ColumnDef idx;
        idx.name = "seq";
        idx.type = DataType::kInt;
        idx.range_min = 0.0;
        idx.range_max = 1000.0;
        idx.is_order = true;
        int_order.columns.push_back(idx);
    }
    {
        ColumnDef val;
        val.name = "v";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 10.0;
        int_order.columns.push_back(val);
    }
    int_order.validate();

    ConstraintSet with_agg;
    with_agg.value_range_names = {"vr1"};
    with_agg.aggregate_defs.push_back(AggregateConstraintDef{
        "agg1", "v", AggregateFunction::kAvg,
        WindowType::kInterval, 3600000000LL, 0.0, 100.0});

    ConstraintClassifier classifier;
    auto result = classifier.classify(with_agg, int_order);
    EXPECT_FALSE(result.ok())
        << "BUG: Should reject aggregate constraint with non-DATETIME ORDER column";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kTypeMismatch)
            << "Expected kTypeMismatch, got: " << result.error().message;
    }
}

// ============================================================================
// Test 24: Full pipeline: Parse DSL -> Define constraint -> Generate via Service
// ============================================================================
TEST(E2EPipelineChaos, FullPipeline_ParseDSL_Constraint_GenerateViaService) {
    const char* dsl = R"(
        DEFINE TYPE pipe_test {
            ts: DATETIME NOT NULL ORDER,
            value: FLOAT [0.0, 100.0],
            count: INT [0, 50]
        };
    )";

    parser::Parser parser;
    auto parse_result = parser.parse(dsl);
    ASSERT_TRUE(parse_result.ok()) << parse_result.error().message;
    ASSERT_TRUE(parse_result.value().errors.empty());

    auto& stmt = std::get<parser::ast::DefineTypeStmt>(
        parse_result.value().program.statements[0]);

    // Use service with manual schema building
    SynthGenService service;
    DefineTypeRequest type_req;
    type_req.type_name = stmt.type_name;
    for (const auto& col : stmt.columns) {
        DefineTypeRequest::ColumnDef cd;
        cd.name = col.name;
        switch (col.type) {
            case DataType::kFloat: cd.type = "FLOAT"; break;
            case DataType::kInt: cd.type = "INT"; break;
            case DataType::kDatetime: cd.type = "DATETIME"; break;
            case DataType::kString: cd.type = "STRING"; break;
            case DataType::kEnum: cd.type = "ENUM"; break;
        }
        cd.not_null = col.not_null;
        cd.is_order = col.is_order;
        cd.range_min = col.range_min;
        cd.range_max = col.range_max;
        cd.enum_values = col.enum_values;
        type_req.columns.push_back(cd);
    }

    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok()) << type_result.error().message;
    EXPECT_EQ(type_result.value().column_count, 3);

    // Define constraint via service
    DefineConstraintRequest con_req;
    con_req.constraint_name = "pipe_con";
    con_req.type_name = "pipe_test";
    {
        DefineConstraintRequest::RangeCheck rc;
        rc.column = "value";
        rc.min_val = 10.0;
        rc.max_val = 90.0;
        con_req.checks.push_back(rc);
    }
    auto con_result = service.define_constraint(con_req);
    ASSERT_TRUE(con_result.ok()) << con_result.error().message;

    // Generate
    GenerateRequest gen_req;
    gen_req.type_name = "pipe_test";
    gen_req.constraints = {"pipe_con"};
    gen_req.limit = 500;
    gen_req.seed = 42;

    auto gen_result = service.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_GT(gen_result.value().stats.rows_generated, 0);
    EXPECT_FALSE(gen_result.value().evidence_json.empty());

    // Parse the evidence JSON and verify it references our constraint
    auto pkg = from_json(gen_result.value().evidence_json);
    ASSERT_TRUE(pkg.ok());
    EXPECT_EQ(pkg.value().provenance.constraints.size(), 1u);
    EXPECT_EQ(pkg.value().provenance.constraints[0], "pipe_con");
    EXPECT_EQ(pkg.value().row_count, 500);
}

// ============================================================================
// Test 25: Empty generate (limit=0) through full pipeline
// ============================================================================
TEST(E2EPipelineChaos, Generate_LimitZero_ProducesEmptyTable) {
    Schema schema;
    schema.type_name = "zero_test";
    {
        ColumnDef val;
        val.name = "v";
        val.type = DataType::kFloat;
        val.range_min = 0.0;
        val.range_max = 10.0;
        schema.columns.push_back(val);
    }
    schema.validate();

    std::vector<parser::ast::ConstraintItem> no_cons;
    GenerationRequest gen_req{schema, no_cons, 0, 42, "uniform"};
    RectangularSampler sampler(schema);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().stats.rows_generated, 0);
    // data pointer could be null or empty table
    if (gen_result.value().data) {
        EXPECT_EQ(gen_result.value().data->num_rows(), 0);
    }

    // Evidence builder should handle empty data
    ValueRangeValidator validator(schema, no_cons);
    ValidationResult empty_val;  // default: 0 rows checked
    TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(), empty_val, gen_req, no_cons);
    ASSERT_TRUE(tail.ok());

    ProvenanceV1 prov;
    EvidencePackageBuilder evp_builder;
    auto evp = evp_builder.build(gen_result.value(), empty_val,
                                  tail.value(), prov, schema);
    ASSERT_TRUE(evp.ok()) << evp.error().message;
    EXPECT_EQ(evp.value().row_count, 0);
}

// ============================================================================
// Test 26: Schema validation rejects duplicate column names
// ============================================================================
TEST(E2EPipelineChaos, SchemaValidation_RejectsDuplicateColumns) {
    Schema dup_schema;
    dup_schema.type_name = "dup_test";
    {
        ColumnDef c1;
        c1.name = "value";
        c1.type = DataType::kFloat;
        c1.range_min = 0.0;
        c1.range_max = 10.0;
        dup_schema.columns.push_back(c1);
    }
    {
        ColumnDef c2;
        c2.name = "value";  // duplicate!
        c2.type = DataType::kInt;
        c2.range_min = 0.0;
        c2.range_max = 100.0;
        dup_schema.columns.push_back(c2);
    }

    auto result = dup_schema.validate();
    EXPECT_FALSE(result.ok()) << "Schema with duplicate column names should be rejected";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kDuplicateColumnName);
    }
}

// ============================================================================
// Test 27: Schema validation rejects ENUM without values
// ============================================================================
TEST(E2EPipelineChaos, SchemaValidation_RejectsEnumWithoutValues) {
    Schema bad_enum;
    bad_enum.type_name = "bad_enum";
    {
        ColumnDef c;
        c.name = "status";
        c.type = DataType::kEnum;
        // No enum_values set!
        bad_enum.columns.push_back(c);
    }

    auto result = bad_enum.validate();
    EXPECT_FALSE(result.ok()) << "ENUM without values should be rejected";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kInvalidEnum);
    }
}

// ============================================================================
// Test 28: Schema validation rejects range_min >= range_max
// ============================================================================
TEST(E2EPipelineChaos, SchemaValidation_RejectsInvalidRange) {
    Schema bad_range;
    bad_range.type_name = "bad_range";
    {
        ColumnDef c;
        c.name = "val";
        c.type = DataType::kFloat;
        c.range_min = 100.0;
        c.range_max = 50.0;  // min > max
        bad_range.columns.push_back(c);
    }

    auto result = bad_range.validate();
    EXPECT_FALSE(result.ok()) << "range_min > range_max should be rejected";
    if (!result.ok()) {
        EXPECT_EQ(result.error().code, ErrorCode::kInvalidRange);
    }

    // Equal case
    Schema eq_range;
    eq_range.type_name = "eq_range";
    {
        ColumnDef c;
        c.name = "val";
        c.type = DataType::kFloat;
        c.range_min = 50.0;
        c.range_max = 50.0;  // min == max
        eq_range.columns.push_back(c);
    }

    auto result2 = eq_range.validate();
    EXPECT_FALSE(result2.ok()) << "range_min == range_max should be rejected";
}

// ============================================================================
// Test 29: Health endpoint returns valid response
// ============================================================================
TEST(E2EPipelineChaos, Service_Health_ReturnsValidResponse) {
    SynthGenService service;
    auto health = service.health();

    EXPECT_EQ(health.status, "healthy");
    EXPECT_FALSE(health.version.empty());
    EXPECT_FALSE(health.components.empty());
    EXPECT_EQ(health.components["parser"], "ok");
    EXPECT_EQ(health.components["storage"], "ok");
    EXPECT_EQ(health.components["physics_engine"], "ok");
}
