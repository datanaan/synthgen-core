#include <gtest/gtest.h>
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "schema/schema_builder.h"
#include "parser/parser.h"
#include "engine/physics/seed_controller.h"
#include "engine/physics/uniform_sampler.h"
#include "engine/physics/gaussian_sampler.h"
#include "engine/physics/random.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/evidence_package.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/evidence/schema_validator.h"
#include "engine/evidence/evidence_package_builder.h"
#include "common/result.h"
#include "common/hash.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <cmath>
#include <limits>
#include <set>

using namespace synthgen;
using namespace synthgen::schema;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::evidence;
using namespace synthgen::scaffold;

// ============================================================================
// Schema Extended Tests
// ============================================================================

TEST(SchemaExtended, ManyColumns) {
    Schema s;
    s.type_name = "wide";
    for (int i = 0; i < 50; ++i) {
        ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = static_cast<double>(i);
        col.range_max = static_cast<double>(i + 1);
        s.columns.push_back(col);
    }
    auto result = s.validate();
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(s.columns.size(), 50u);
    EXPECT_EQ(s.column_index("col_49"), 49);
}

TEST(SchemaExtended, NoOrderColumns) {
    Schema s;
    s.type_name = "no_order";
    s.columns.push_back({});
    s.columns.back().name = "x";
    s.columns.back().type = DataType::kFloat;
    auto oc = s.order_columns();
    EXPECT_TRUE(oc.empty());
}

TEST(SchemaExtended, MultipleOrderColumns) {
    Schema s;
    s.type_name = "multi_order";
    for (int i = 0; i < 3; ++i) {
        ColumnDef col;
        col.name = "ts" + std::to_string(i);
        col.type = DataType::kDatetime;
        col.is_order = true;
        s.columns.push_back(col);
    }
    auto oc = s.order_columns();
    EXPECT_EQ(oc.size(), 3u);
}

TEST(SchemaExtended, IntColumnWithRange) {
    Schema s;
    s.type_name = "int_test";
    ColumnDef col;
    col.name = "count";
    col.type = DataType::kInt;
    col.range_min = 0;
    col.range_max = 100;
    s.columns.push_back(col);
    auto result = s.validate();
    EXPECT_TRUE(result.ok());
}

TEST(SchemaExtended, StringColumnNoRange) {
    Schema s;
    s.type_name = "str_test";
    ColumnDef col;
    col.name = "name";
    col.type = DataType::kString;
    s.columns.push_back(col);
    auto result = s.validate();
    EXPECT_TRUE(result.ok());
}

TEST(SchemaExtended, FindColumnCaseSensitive) {
    Schema s;
    s.type_name = "test";
    ColumnDef col;
    col.name = "Temperature";
    col.type = DataType::kFloat;
    s.columns.push_back(col);
    EXPECT_TRUE(s.find_column("Temperature").has_value());
    EXPECT_FALSE(s.find_column("temperature").has_value());
}

TEST(SchemaExtended, RegistryMultipleSchemas) {
    SchemaRegistry reg;
    for (int i = 0; i < 10; ++i) {
        Schema s;
        s.type_name = "type_" + std::to_string(i);
        ColumnDef col;
        col.name = "x";
        col.type = DataType::kFloat;
        s.columns.push_back(col);
        auto r = reg.register_schema(std::move(s));
        ASSERT_TRUE(r.ok()) << "Failed at i=" << i;
    }
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(reg.has_schema("type_" + std::to_string(i)));
    }
    EXPECT_FALSE(reg.has_schema("type_99"));
}

// ============================================================================
// Seed Controller Extended Tests
// ============================================================================

TEST(SeedExtended, RowSeedDeterminism) {
    SeedController sc1(42);
    SeedController sc2(42);
    auto rs1 = sc1.request_seed(1);
    auto bs1 = sc1.batch_seed(rs1, 0);
    auto rs2 = sc2.request_seed(1);
    auto bs2 = sc2.batch_seed(rs2, 0);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(sc1.row_seed(bs1, i), sc2.row_seed(bs2, i));
    }
}

TEST(SeedExtended, DifferentRowDifferentSeed) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    auto bs = sc.batch_seed(rs, 0);
    auto s1 = sc.row_seed(bs, 0);
    auto s2 = sc.row_seed(bs, 1);
    auto s3 = sc.row_seed(bs, 2);
    EXPECT_NE(s1, s2);
    EXPECT_NE(s2, s3);
    EXPECT_NE(s1, s3);
}

TEST(SeedExtended, LargeBatchIndex) {
    SeedController sc(42);
    auto rs = sc.request_seed(1);
    auto bs = sc.batch_seed(rs, 1000000);
    auto ws = sc.row_seed(bs, 999999);
    EXPECT_NE(ws, 0u);
}

TEST(SeedExtended, SeedDeterminism1000Requests) {
    SeedController sc(12345);
    std::vector<uint64_t> expected;
    for (int i = 0; i < 1000; ++i) {
        expected.push_back(sc.request_seed(i));
    }
    SeedController sc2(12345);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(sc2.request_seed(i), expected[i]) << "Mismatch at request " << i;
    }
}

// ============================================================================
// Distribution Extended Tests
// ============================================================================

TEST(DistributionExtended, UniformIntSameMinMax) {
    UniformSampler s(42);
    for (int i = 0; i < 100; ++i) {
        auto val = s.sample_int(5, 5);
        EXPECT_EQ(val, 5);
    }
}

TEST(DistributionExtended, UniformIntNegativeRange) {
    UniformSampler s(42);
    for (int i = 0; i < 100; ++i) {
        auto val = s.sample_int(-100, -50);
        EXPECT_GE(val, -100);
        EXPECT_LE(val, -50);
    }
}

TEST(DistributionExtended, UniformDatetimeRange) {
    UniformSampler s(42);
    for (int i = 0; i < 100; ++i) {
        auto val = s.sample_datetime();
        EXPECT_GE(val, 0);
        EXPECT_LE(val, 31536000000000LL);
    }
}

TEST(DistributionExtended, UniformEnumLargeList) {
    std::vector<std::string> values;
    for (int i = 0; i < 50; ++i) values.push_back("val_" + std::to_string(i));
    UniformSampler s(42);
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        auto val = s.sample_enum(values);
        EXPECT_NE(std::find(values.begin(), values.end(), val), values.end());
        seen.insert(val);
    }
    // With 1000 samples from 50 values, should see most
    EXPECT_GT(seen.size(), 30u);
}

TEST(DistributionExtended, RandomEngineUniform01) {
    RandomEngine r(42);
    for (int i = 0; i < 1000; ++i) {
        auto val = r.uniform_01();
        EXPECT_GE(val, 0.0);
        EXPECT_LT(val, 1.0);
    }
}

TEST(DistributionExtended, RandomEngineUniformRange) {
    RandomEngine r(42);
    for (int i = 0; i < 1000; ++i) {
        auto val = r.uniform_range(-100.0, 100.0);
        EXPECT_GE(val, -100.0);
        EXPECT_LE(val, 100.0);
    }
}

TEST(DistributionExtended, GaussianTruncationStats) {
    GaussianSampler s(42);
    TruncationStats stats;
    for (int i = 0; i < 10000; ++i) {
        s.sample_float(0.0, 1.0, stats);
    }
    EXPECT_GT(stats.truncated_low + stats.truncated_high, 0);
}

// ============================================================================
// Rectangular Sampler Extended Tests
// ============================================================================

namespace {

Schema make_sensor_schema() {
    Schema s;
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

std::vector<parser::ast::ConstraintItem> make_constraints() {
    return {{"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0}};
}

}  // namespace

TEST(SamplerExtended, TightConstraints) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    // Very tight constraint range
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, 20.0, 21.0}
    };
    GenerationRequest req{schema, constraints, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 100);

    auto table = result.value().data;
    auto temp_col = std::static_pointer_cast<arrow::DoubleArray>(table->column(0)->chunk(0));
    for (int64_t i = 0; i < temp_col->length(); ++i) {
        EXPECT_GE(temp_col->Value(i), 20.0);
        EXPECT_LE(temp_col->Value(i), 21.0);
    }
}

TEST(SamplerExtended, WideConstraints) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -1000.0, 1000.0}
    };
    GenerationRequest req{schema, constraints, 100, 42, "uniform", 1000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().stats.rows_generated, 100);
}

TEST(SamplerExtended, GenerateLargeBatch) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    GenerationRequest req{schema, {}, 10000, 42, "uniform", 5000};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().stats.rows_generated, 10000);
    EXPECT_EQ(result.value().data->num_rows(), 10000);
}

TEST(SamplerExtended, GenerateWithDifferentSeeds) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);

    GenerationRequest req1{schema, {}, 100, 42, "uniform", 1000};
    GenerationRequest req2{schema, {}, 100, 999, "uniform", 1000};
    auto r1 = sampler.generate(req1);
    auto r2 = sampler.generate(req2);
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());

    auto col1 = std::static_pointer_cast<arrow::DoubleArray>(r1.value().data->column(0)->chunk(0));
    auto col2 = std::static_pointer_cast<arrow::DoubleArray>(r2.value().data->column(0)->chunk(0));
    int differences = 0;
    for (int64_t i = 0; i < col1->length(); ++i) {
        if (col1->Value(i) != col2->Value(i)) differences++;
    }
    EXPECT_GT(differences, 50);  // Most values should differ
}

TEST(SamplerExtended, BatchSizeAffectsBatchCount) {
    auto schema = make_sensor_schema();
    RectangularSampler sampler(schema);
    // 1000 rows with batch_size 100 → 10 batches
    GenerationRequest req{schema, {}, 1000, 42, "uniform", 100};
    auto result = sampler.generate(req);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().stats.batch_count, 10);
}

// ============================================================================
// Value Range Validator Extended Tests
// ============================================================================

namespace {

Schema make_validation_schema() {
    Schema s;
    s.type_name = "test";
    ColumnDef temp;
    temp.name = "temperature";
    temp.type = DataType::kFloat;
    temp.range_min = -50.0;
    temp.range_max = 80.0;
    s.columns.push_back(temp);
    return s;
}

std::shared_ptr<arrow::Table> make_batch(const std::vector<double>& temps) {
    arrow::DoubleBuilder builder;
    for (auto v : temps) builder.Append(v);
    std::shared_ptr<arrow::Array> arr;
    builder.Finish(&arr);
    return arrow::Table::Make(
        arrow::schema({arrow::field("temperature", arrow::float64())}),
        {arr});
}

std::vector<parser::ast::ConstraintItem> make_constraints(double min_val, double max_val) {
    return {{"temperature", parser::ast::ConstraintOperator::kBetween, min_val, max_val}};
}

}  // namespace

TEST(ValidationExtended, AllRowsFail) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(0.0, 1.0);
    ValueRangeValidator validator(schema, constraints);
    // All values out of range
    auto batch = make_batch({100.0, 200.0, 300.0, 400.0, 500.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 0);
    EXPECT_EQ(result.value().rows_failed, 5);
    EXPECT_DOUBLE_EQ(result.value().pass_rate, 0.0);
}

TEST(ValidationExtended, HalfFail) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(0.0, 25.0);
    ValueRangeValidator validator(schema, constraints);
    auto batch = make_batch({10.0, 20.0, 30.0, 40.0, 50.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 2);
    EXPECT_EQ(result.value().rows_failed, 3);
}

TEST(ValidationExtended, SingleRowPass) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(-10.0, 45.0);
    ValueRangeValidator validator(schema, constraints);
    auto batch = make_batch({25.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 1);
    EXPECT_EQ(result.value().rows_failed, 0);
}

TEST(ValidationExtended, SingleRowFail) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(-10.0, 45.0);
    ValueRangeValidator validator(schema, constraints);
    auto batch = make_batch({100.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 0);
    EXPECT_EQ(result.value().rows_failed, 1);
}

TEST(ValidationExtended, MultipleConstraints) {
    auto schema = make_validation_schema();
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0},
        {"temperature", parser::ast::ConstraintOperator::kGreaterThan, 0.0}
    };
    ValueRangeValidator validator(schema, constraints);
    // -5.0 passes BETWEEN but fails > 0
    auto batch = make_batch({-5.0, 10.0, 25.0});
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().rows_failed, 0);
}

TEST(ValidationExtended, LargeBatchValidation) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(-10.0, 45.0);
    ValueRangeValidator validator(schema, constraints);
    std::vector<double> temps(10000, 25.0);  // All in range
    auto batch = make_batch(temps);
    auto result = validator.validate_batch(batch);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_checked, 10000);
    EXPECT_EQ(result.value().rows_failed, 0);
}

TEST(ValidationExtended, ExplainReturnsCorrectInfo) {
    auto schema = make_validation_schema();
    auto constraints = make_constraints(-10.0, 45.0);
    ValueRangeValidator validator(schema, constraints);
    auto info = validator.explain();
    EXPECT_EQ(info.path, "value_range_validation");
    EXPECT_EQ(info.constraint_classification.value_range, 1);
}

// ============================================================================
// Hash Extended Tests
// ============================================================================

TEST(HashExtended, EmptyStringHash) {
    auto h = sha256_hex("");
    EXPECT_EQ(h.size(), 64u);
}

TEST(HashExtended, LongInputHash) {
    std::string long_input(10000, 'a');
    auto h = sha256_hex(long_input);
    EXPECT_EQ(h.size(), 64u);
}

TEST(HashExtended, SpecialCharactersHash) {
    auto h = sha256_hex("hello\nworld\t!@#$%^&*()");
    EXPECT_EQ(h.size(), 64u);
}

TEST(HashExtended, ConsistentHashMultipleCalls) {
    for (int i = 0; i < 100; ++i) {
        auto h1 = sha256_hex("test_input_" + std::to_string(i));
        auto h2 = sha256_hex("test_input_" + std::to_string(i));
        EXPECT_EQ(h1, h2);
    }
}

// ============================================================================
// Result<T> Extended Tests
// ============================================================================

TEST(ResultExtended, ResultInt) {
    Result<int> r(42);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultExtended, ResultString) {
    Result<std::string> r("hello");
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.value(), "hello");
}

TEST(ResultExtended, ResultError) {
    Result<int> r = Error(ErrorCode::kInvalidArgument, "test error", "test");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
    EXPECT_EQ(r.error().message, "test error");
}

TEST(ResultExtended, ResultVoid) {
    Result<void> r;
    EXPECT_TRUE(r.ok());
}

TEST(ResultExtended, ResultVoidError) {
    Result<void> r = Error(ErrorCode::kInvalidState, "void error", "test");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidState);
}

TEST(ResultExtended, ResultBool) {
    Result<bool> r(true);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.value());
}

// ============================================================================
// Scaffold Extended Tests
// ============================================================================

TEST(ScaffoldExtended, MultipleSpans) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard g1("a", "op1", "t1");
        SpanGuard g2("b", "op2", "t1");
        SpanGuard g3("c", "op3", "t1");
    }
    EXPECT_EQ(SpanGuard::active_spans().size(), 3u);
}

TEST(ScaffoldExtended, SpanAttributeOverwrite) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "op", "t");
        guard.set_attribute("key", "value1");
        guard.set_attribute("key", "value2");
    }
    EXPECT_EQ(SpanGuard::active_spans()[0].attributes.at("key"), "value2");
}

TEST(ScaffoldExtended, SpanTiming) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "op", "t");
        // Span should have positive duration
    }
    auto& span = SpanGuard::active_spans()[0];
    EXPECT_GT(span.end_time_us, 0);
    EXPECT_GE(span.end_time_us, span.start_time_us);
}

TEST(ScaffoldExtended, CounterLargeValue) {
    MetricsRegistry::instance().reset();
    auto& c = MetricsRegistry::instance().counter("big_counter");
    c.increment(INT64_MAX - 100);
    EXPECT_GT(c.value(), 0);
}

TEST(ScaffoldExtended, GaugeNegativeValue) {
    MetricsRegistry::instance().reset();
    auto& g = MetricsRegistry::instance().gauge("neg_gauge");
    g.set(-42.5);
    EXPECT_DOUBLE_EQ(g.value(), -42.5);
}

TEST(ScaffoldExtended, HistogramManyObservations) {
    MetricsRegistry::instance().reset();
    auto& h = MetricsRegistry::instance().histogram("many_obs");
    for (int i = 0; i < 1000; ++i) {
        h.observe(static_cast<double>(i));
    }
    EXPECT_EQ(h.count(), 1000);
    EXPECT_DOUBLE_EQ(h.sum(), 999.0 * 1000.0 / 2.0);  // sum of 0..999
    EXPECT_DOUBLE_EQ(h.mean(), 499.5);
}

TEST(ScaffoldExtended, GaugeNames) {
    MetricsRegistry::instance().reset();
    auto& g = MetricsRegistry::instance().gauge("test_gauge_name");
    EXPECT_EQ(g.name(), "test_gauge_name");
}

TEST(ScaffoldExtended, HistogramNames) {
    MetricsRegistry::instance().reset();
    auto& h = MetricsRegistry::instance().histogram("test_hist_name");
    EXPECT_EQ(h.name(), "test_hist_name");
}

TEST(ScaffoldExtended, AllGaugesAndCounters) {
    MetricsRegistry::instance().reset();
    MetricsRegistry::instance().counter("c1").increment(1);
    MetricsRegistry::instance().gauge("g1").set(10.0);
    MetricsRegistry::instance().histogram("h1").observe(5.0);
    auto counters = MetricsRegistry::instance().all_counters();
    auto gauges = MetricsRegistry::instance().all_gauges();
    EXPECT_EQ(counters.size(), 1u);
    EXPECT_EQ(gauges.size(), 1u);
}

// ============================================================================
// ExplainInfo Extended Tests
// ============================================================================

TEST(ExplainExtended, ExecutionModeValues) {
    EXPECT_EQ(ExecutionMode::kRowByRow, ExecutionMode::kRowByRow);
    EXPECT_NE(ExecutionMode::kRowByRow, ExecutionMode::kStatefulBatch);
    EXPECT_NE(ExecutionMode::kStatefulBatch, ExecutionMode::kTwoPhase);
}

TEST(ExplainExtended, ConstraintClassificationDefaults) {
    ConstraintClassification cc;
    EXPECT_EQ(cc.value_range, 0);
    EXPECT_EQ(cc.inter_row, 0);
    EXPECT_EQ(cc.aggregate, 0);
}

// ============================================================================
// Evidence Package V1 Extended Tests
// ============================================================================

TEST(EvidenceV1Extended, ProvenanceDefaultFields) {
    ProvenanceV1 prov;
    EXPECT_TRUE(prov.data_source.empty());
    EXPECT_TRUE(prov.generator_identity.empty());
    EXPECT_TRUE(prov.constraints.empty());
    EXPECT_TRUE(prov.trace_spans.empty());
}

TEST(EvidenceV1Extended, GenerationParamsDefault) {
    GenerationParams gp;
    EXPECT_EQ(gp.seed, 0u);
    EXPECT_EQ(gp.distribution, "uniform");
    EXPECT_EQ(gp.limit, 0);
    EXPECT_EQ(gp.batch_size, 1000);
}

TEST(EvidenceV1Extended, TraceSpanEntryDefaults) {
    TraceSpanEntry entry;
    EXPECT_TRUE(entry.trace_id.empty());
    EXPECT_TRUE(entry.span_id.empty());
    EXPECT_TRUE(entry.component.empty());
    EXPECT_TRUE(entry.operation.empty());
    EXPECT_EQ(entry.status, "ok");
}

TEST(EvidenceV1Extended, MultipleTraceSpansSerialization) {
    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = "abc123";
    pkg.exclusion_rate = 0.0;
    pkg.data_grade = "physics_guaranteed";
    pkg.row_count = 10;
    pkg.epistemological_bias = "physical_first";
    pkg.tail_exclusion_statement = "test";
    pkg.audit_immutability = "not_applicable";
    pkg.statistical_fidelity = "not_applicable";
    pkg.drift_detection = "not_applicable";
    pkg.constraint_type_breakdown = "not_applicable";
    pkg.provenance.trace_spans = {
        {"t1", "s1", "parser", "parse", "ok"},
        {"t1", "s2", "engine", "generate", "ok"},
        {"t1", "s3", "validator", "validate", "ok"}
    };

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    auto parsed = from_json(json_result.value());
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().provenance.trace_spans.size(), 3u);
}

TEST(EvidenceV1Extended, ConstraintSummaryMultipleDetails) {
    EvidencePackageV1 pkg;
    pkg.schema_version = "v1";
    pkg.schema_hash = "abc123";
    pkg.exclusion_rate = 0.0;
    pkg.data_grade = "physics_guaranteed";
    pkg.row_count = 10;
    pkg.epistemological_bias = "physical_first";
    pkg.tail_exclusion_statement = "test";
    pkg.audit_immutability = "not_applicable";
    pkg.statistical_fidelity = "not_applicable";
    pkg.drift_detection = "not_applicable";
    pkg.constraint_type_breakdown = "not_applicable";
    pkg.constraint_summary.type = "value_range";
    pkg.constraint_summary.details = {
        {"temp", -10.0, 45.0},
        {"pressure", 900.0, 1100.0},
        {"vibration", 0.0, 10.0}
    };

    auto json_result = to_json(pkg);
    ASSERT_TRUE(json_result.ok());
    auto parsed = from_json(json_result.value());
    ASSERT_TRUE(parsed.ok());
    EXPECT_EQ(parsed.value().constraint_summary.details.size(), 3u);
}

// ============================================================================
// Parser Extended Tests
// ============================================================================

TEST(ParserExtended, MultipleTypeDefinitions) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE t1 { x: FLOAT };"
        "DEFINE TYPE t2 { y: INT };"
        "DEFINE TYPE t3 { z: STRING };");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().program.statements.size(), 3u);
}

TEST(ParserExtended, DefineTypeWithAllColumnTypes) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE full {"
        "  f: FLOAT [0.0, 1.0],"
        "  i: INT,"
        "  d: DATETIME,"
        "  s: STRING,"
        "  e: ENUM('a', 'b', 'c')"
        "};");
    ASSERT_TRUE(result.ok()) << result.error().message;
    auto* stmt = std::get_if<parser::ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->columns.size(), 5u);
}

TEST(ParserExtended, ConstraintWithMultipleItems) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE t { a: FLOAT, b: FLOAT, c: FLOAT };"
        "DEFINE CONSTRAINT multi ON t { a BETWEEN 0 AND 10, b > -5, c < 100 };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<parser::ast::DefineConstraintStmt>(
        &result.value().program.statements[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->items.size(), 3u);
}

TEST(ParserExtended, FullPipelineParsing) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE sensor { temp: FLOAT [-50.0, 80.0] };"
        "DEFINE CONSTRAINT safe ON sensor { temp BETWEEN -10 AND 45 };"
        "GENERATE TABLE output FROM sensor WITH CONSTRAINTS safe LIMIT 1000;");
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().program.statements.size(), 3u);
}

TEST(ParserExtended, MultipleConstraints) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c1 ON t { x BETWEEN 0 AND 10 };"
        "DEFINE CONSTRAINT c2 ON t { x > -5 };");
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().program.statements.size(), 3u);
}

TEST(ParserExtended, NestedCurlyBraces) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE t { x: FLOAT };"
        "DEFINE CONSTRAINT c ON t { x BETWEEN 0 AND 1 };");
    ASSERT_TRUE(result.ok());
    // Verify no stray errors about braces
    EXPECT_TRUE(result.value().errors.empty());
}

TEST(ParserExtended, WhitespaceOnlyInput) {
    parser::Parser p;
    auto result = p.parse("   \n\t\n   ");
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().program.statements.empty());
}

TEST(ParserExtended, DefineTypeWithIntColumn) {
    parser::Parser p;
    auto result = p.parse("DEFINE TYPE t { count: INT };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<parser::ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->columns[0].type, DataType::kInt);
}

TEST(ParserExtended, DefineTypeWithDatetimeColumn) {
    parser::Parser p;
    auto result = p.parse("DEFINE TYPE t { ts: DATETIME };");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<parser::ast::DefineTypeStmt>(&result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->columns[0].type, DataType::kDatetime);
}

TEST(ParserExtended, LoadDataWithStrictMode) {
    parser::Parser p;
    auto result = p.parse(
        "DEFINE TYPE t { x: FLOAT };"
        "LOAD DATA INTO t FROM '/data/file.parquet';");
    ASSERT_TRUE(result.ok());
    auto* stmt = std::get_if<parser::ast::LoadDataStmt>(&result.value().program.statements[1]);
    ASSERT_NE(stmt, nullptr);
    EXPECT_EQ(stmt->type_name, "t");
    EXPECT_EQ(stmt->file_path, "/data/file.parquet");
}
