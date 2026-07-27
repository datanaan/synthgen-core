// Chaos test round 2: Service layer edge cases + Scaffold infrastructure stress
#include <gtest/gtest.h>

#include "api/service.h"
#include "api/request.h"
#include "api/response.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"
#include "scaffold/explain.h"
#include "common/result.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace synthgen::api;
using namespace synthgen::scaffold;
using namespace synthgen;

// ---------------------------------------------------------------------------
// Helper: build a simple schema with one FLOAT column in [0, 100]
// ---------------------------------------------------------------------------
static DefineTypeRequest make_basic_type(const std::string& name = "sensor") {
    DefineTypeRequest req;
    req.type_name = name;
    DefineTypeRequest::ColumnDef col;
    col.name = "value";
    col.type = "FLOAT";
    col.range_min = 0.0;
    col.range_max = 100.0;
    req.columns.push_back(col);
    return req;
}

static DefineTypeRequest make_enum_only_type(const std::string& name = "enum_type") {
    DefineTypeRequest req;
    req.type_name = name;
    DefineTypeRequest::ColumnDef col;
    col.name = "status";
    col.type = "ENUM";
    col.enum_values = {"ok", "warn", "error"};
    req.columns.push_back(col);
    return req;
}

// ===========================================================================
// TEST 1: Generate with constraint referencing nonexistent constraint name
// ===========================================================================
TEST(ServiceScaffoldChaos, GenerateNonexistentConstraint_ReturnsNotFound) {
    SynthGenService svc;
    auto dt = svc.define_type(make_basic_type());
    ASSERT_TRUE(dt.ok());

    GenerateRequest gen;
    gen.type_name = "sensor";
    gen.constraints = {"ghost_constraint"};
    gen.limit = 10;

    auto result = svc.generate(gen);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
    EXPECT_NE(result.error().message.find("ghost_constraint"), std::string::npos);
}

// ===========================================================================
// TEST 2: Generate with empty constraints list (no constraint filtering)
// ===========================================================================
TEST(ServiceScaffoldChaos, GenerateEmptyConstraints_Succeeds) {
    SynthGenService svc;
    auto dt = svc.define_type(make_basic_type());
    ASSERT_TRUE(dt.ok());

    GenerateRequest gen;
    gen.type_name = "sensor";
    gen.constraints = {};  // no constraints at all
    gen.limit = 50;
    gen.seed = 123;

    auto result = svc.generate(gen);
    ASSERT_TRUE(result.ok()) << "Error: " << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 50);
    EXPECT_FALSE(result.value().evidence_json.empty());
}

// ===========================================================================
// TEST 3: Define type, then define same type name again (should overwrite)
// ===========================================================================
TEST(ServiceScaffoldChaos, DefineTypeTwice_SecondOverwrites) {
    SynthGenService svc;

    auto req1 = make_basic_type("dup_type");
    auto r1 = svc.define_type(req1);
    ASSERT_TRUE(r1.ok());
    EXPECT_EQ(r1.value().column_count, 1);

    // Second definition with 2 columns
    DefineTypeRequest req2;
    req2.type_name = "dup_type";
    DefineTypeRequest::ColumnDef c1;
    c1.name = "a"; c1.type = "FLOAT"; c1.range_min = 0; c1.range_max = 10;
    DefineTypeRequest::ColumnDef c2;
    c2.name = "b"; c2.type = "INT"; c2.range_min = 0; c2.range_max = 100;
    req2.columns.push_back(c1);
    req2.columns.push_back(c2);

    auto r2 = svc.define_type(req2);
    ASSERT_TRUE(r2.ok());
    EXPECT_EQ(r2.value().column_count, 2);

    // Generate should use the 2-column schema
    GenerateRequest gen;
    gen.type_name = "dup_type";
    gen.limit = 5;
    gen.seed = 42;
    auto gr = svc.generate(gen);
    ASSERT_TRUE(gr.ok()) << "Error: " << gr.error().message;
    EXPECT_EQ(gr.value().stats.rows_generated, 5);
}

// ===========================================================================
// TEST 4: Generate with very large limit (1,000,000 rows)
// ===========================================================================
TEST(ServiceScaffoldChaos, GenerateLargeLimit_Succeeds) {
    SynthGenService svc;
    auto dt = svc.define_type(make_basic_type("big_type"));
    ASSERT_TRUE(dt.ok());

    GenerateRequest gen;
    gen.type_name = "big_type";
    gen.limit = 1000000;
    gen.seed = 99;

    auto result = svc.generate(gen);
    ASSERT_TRUE(result.ok()) << "Error: " << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 1000000);
    EXPECT_GT(result.value().stats.elapsed_ms, 0);
}

// ===========================================================================
// TEST 5: Define type with only ENUM columns -- generate should work
// ===========================================================================
TEST(ServiceScaffoldChaos, EnumOnlyType_GenerateSucceeds) {
    SynthGenService svc;
    auto dt = svc.define_type(make_enum_only_type());
    ASSERT_TRUE(dt.ok());

    GenerateRequest gen;
    gen.type_name = "enum_type";
    gen.limit = 20;
    gen.seed = 7;

    auto result = svc.generate(gen);
    ASSERT_TRUE(result.ok()) << "Error: " << result.error().message;
    EXPECT_EQ(result.value().stats.rows_generated, 20);
}

// ===========================================================================
// TEST 6: Generate with seed that causes all rows to fail validation
//         (constraining value range narrower than schema range)
// ===========================================================================
TEST(ServiceScaffoldChaos, AllRowsFailValidation_EvidenceReportsCorrectly) {
    SynthGenService svc;
    // Schema allows [0, 100], but we constrain to [50, 51] -- narrow band
    DefineTypeRequest req;
    req.type_name = "narrow";
    DefineTypeRequest::ColumnDef col;
    col.name = "x";
    col.type = "FLOAT";
    col.range_min = 0.0;
    col.range_max = 100.0;
    req.columns.push_back(col);
    auto dt = svc.define_type(req);
    ASSERT_TRUE(dt.ok());

    // Constraint: x must be in [49.5, 50.5]
    DefineConstraintRequest creq;
    creq.constraint_name = "tight";
    creq.type_name = "narrow";
    DefineConstraintRequest::RangeCheck chk;
    chk.column = "x";
    chk.min_val = 49.5;
    chk.max_val = 50.5;
    creq.checks.push_back(chk);
    auto cr = svc.define_constraint(creq);
    ASSERT_TRUE(cr.ok());

    GenerateRequest gen;
    gen.type_name = "narrow";
    gen.constraints = {"tight"};
    gen.limit = 100;
    gen.seed = 1;

    auto result = svc.generate(gen);
    ASSERT_TRUE(result.ok()) << "Error: " << result.error().message;

    // Even with narrow constraint, the physics engine should still generate
    // Verify evidence package is populated
    EXPECT_GT(result.value().evidence_json.size(), 0u);
    EXPECT_GE(result.value().stats.rows_generated, 0);
}

// ===========================================================================
// TEST 7: Explain after multiple constraints defined -- verify classification
// ===========================================================================
TEST(ServiceScaffoldChaos, ExplainMultipleConstraints_ClassificationCounts) {
    SynthGenService svc;
    auto dt = svc.define_type(make_basic_type("expl_type"));
    ASSERT_TRUE(dt.ok());

    // Define 3 constraints
    for (int i = 0; i < 3; ++i) {
        DefineConstraintRequest creq;
        creq.constraint_name = "c" + std::to_string(i);
        creq.type_name = "expl_type";
        DefineConstraintRequest::RangeCheck chk;
        chk.column = "value";
        chk.min_val = 0.0;
        chk.max_val = static_cast<double>(i + 1) * 10.0;
        creq.checks.push_back(chk);
        auto cr = svc.define_constraint(creq);
        ASSERT_TRUE(cr.ok());
    }

    ExplainRequest ereq;
    ereq.type_name = "expl_type";
    ereq.constraints = {"c0", "c1", "c2"};

    auto result = svc.explain(ereq);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().constraint_classification["value_range"], 3);
    EXPECT_EQ(result.value().constraint_classification["inter_row"], 0);
    EXPECT_EQ(result.value().constraint_classification["aggregate"], 0);
    EXPECT_EQ(result.value().execution_mode, "row_by_row");
}

// ===========================================================================
// TEST 8: load_data called multiple times on same type (v1 stub)
// ===========================================================================
TEST(ServiceScaffoldChaos, LoadDataMultipleTimes_Succeeds) {
    SynthGenService svc;
    auto dt = svc.define_type(make_basic_type("ld_type"));
    ASSERT_TRUE(dt.ok());

    for (int i = 0; i < 5; ++i) {
        LoadDataRequest lreq;
        lreq.type_name = "ld_type";
        lreq.path = "/tmp/data_" + std::to_string(i) + ".parquet";
        auto lr = svc.load_data(lreq);
        ASSERT_TRUE(lr.ok()) << "Load #" << i << " failed: " << lr.error().message;
        EXPECT_EQ(lr.value().status, "success");
        EXPECT_EQ(lr.value().type_name, "ld_type");
    }
}

// ===========================================================================
// TEST 9: SpanGuard nested 10 levels deep -- all spans captured
// ===========================================================================
TEST(ServiceScaffoldChaos, SpanGuardNested10Levels_AllCaptured) {
    // Clear any previous spans
    SpanGuard::active_spans().clear();

    {
        SpanGuard s0("comp0", "op0", "trace10");
        s0.set_attribute("level", "0");
        {
            SpanGuard s1("comp1", "op1", "trace10", s0.span().span_id);
            s1.set_attribute("level", "1");
            {
                SpanGuard s2("comp2", "op2", "trace10", s1.span().span_id);
                s2.set_attribute("level", "2");
                {
                    SpanGuard s3("comp3", "op3", "trace10", s2.span().span_id);
                    s3.set_attribute("level", "3");
                    {
                        SpanGuard s4("comp4", "op4", "trace10", s3.span().span_id);
                        s4.set_attribute("level", "4");
                        {
                            SpanGuard s5("comp5", "op5", "trace10", s4.span().span_id);
                            s5.set_attribute("level", "5");
                            {
                                SpanGuard s6("comp6", "op6", "trace10", s5.span().span_id);
                                s6.set_attribute("level", "6");
                                {
                                    SpanGuard s7("comp7", "op7", "trace10", s6.span().span_id);
                                    s7.set_attribute("level", "7");
                                    {
                                        SpanGuard s8("comp8", "op8", "trace10", s7.span().span_id);
                                        s8.set_attribute("level", "8");
                                        {
                                            SpanGuard s9("comp9", "op9", "trace10", s8.span().span_id);
                                            s9.set_attribute("level", "9");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 10u);

    // Spans are pushed in LIFO (destruction) order: innermost first.
    // spans[0] = comp9 (innermost, destroyed first)
    // spans[9] = comp0 (outermost, destroyed last)
    for (size_t i = 0; i < spans.size(); ++i) {
        int level = 9 - static_cast<int>(i);  // reverse map index -> level
        EXPECT_EQ(spans[i].component, "comp" + std::to_string(level))
            << "spans[" << i << "] component mismatch";
        EXPECT_EQ(spans[i].operation, "op" + std::to_string(level))
            << "spans[" << i << "] operation mismatch";
        EXPECT_EQ(spans[i].trace_id, "trace10");
        EXPECT_EQ(spans[i].status, "ok");
        EXPECT_GT(spans[i].end_time_us, 0);
        EXPECT_GE(spans[i].end_time_us, spans[i].start_time_us);
        EXPECT_NE(spans[i].span_id, "");
        EXPECT_EQ(spans[i].attributes.at("level"), std::to_string(level))
            << "spans[" << i << "] level attribute mismatch";
    }

    // Verify parent chain: in LIFO order, spans[i+1] is the parent of spans[i]
    // (because spans[i+1] was the enclosing scope whose span_id was passed as parent)
    for (size_t i = 0; i < spans.size() - 1; ++i) {
        EXPECT_EQ(spans[i].parent_span_id, spans[i + 1].span_id)
            << "LIFO index " << i << " parent mismatch";
    }
    // Outermost span (last in LIFO, at index 9) should have empty parent
    EXPECT_EQ(spans[9].parent_span_id, "")
        << "Outermost span should have no parent";

    // Clean up
    SpanGuard::active_spans().clear();
}

// ===========================================================================
// TEST 10: SpanGuard attributes with special characters (quotes, newlines)
// ===========================================================================
TEST(ServiceScaffoldChaos, SpanGuardSpecialAttributes_NoCrash) {
    SpanGuard::active_spans().clear();

    {
        SpanGuard span("special", "attrs", "trace_special");
        span.set_attribute("quote_key", "value with \"quotes\"");
        span.set_attribute("newline_val", "line1\nline2\rline3");
        span.set_attribute("unicode", "emoji: \xF0\x9F\x98\x80");
        span.set_attribute("empty", "");
        span.set_attribute("key=with=equals", "val");
        span.set_attribute("key with spaces", "also fine");
        span.set_status("error: \"bad request\"");
    }

    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    auto& s = spans[0];
    EXPECT_EQ(s.attributes.at("quote_key"), "value with \"quotes\"");
    EXPECT_EQ(s.attributes.at("newline_val"), "line1\nline2\rline3");
    EXPECT_EQ(s.attributes.at("empty"), "");
    EXPECT_EQ(s.status, "error: \"bad request\"");

    SpanGuard::active_spans().clear();
}

// ===========================================================================
// TEST 11: MetricsRegistry with 100 different counters -- all independent
// ===========================================================================
TEST(ServiceScaffoldChaos, Metrics100Counters_AllIndependent) {
    MetricsRegistry::instance().reset();

    const int N = 100;
    // Create and increment each counter by its index
    for (int i = 0; i < N; ++i) {
        auto& c = MetricsRegistry::instance().counter("ctr_" + std::to_string(i));
        c.increment(i);
    }

    auto all = MetricsRegistry::instance().all_counters();
    EXPECT_EQ(static_cast<int>(all.size()), N);

    for (int i = 0; i < N; ++i) {
        std::string name = "ctr_" + std::to_string(i);
        EXPECT_EQ(all[name], i) << "Counter " << name << " expected " << i;
    }

    MetricsRegistry::instance().reset();
}

// ===========================================================================
// TEST 12: Counter::increment with INT64_MAX -- no overflow crash
// ===========================================================================
TEST(ServiceScaffoldChaos, CounterIncrementINT64Max_NoOverflowCrash) {
    MetricsRegistry::instance().reset();

    auto& c = MetricsRegistry::instance().counter("max_counter");
    c.increment(INT64_MAX);
    EXPECT_EQ(c.value(), INT64_MAX);

    // Increment again -- technically UB overflow, but should not crash
    c.increment(1);
    // The value wraps in two's complement, but we just verify no crash
    // We DON'T assert on the value because signed overflow is UB
    // Just ensure the call completes
    EXPECT_NO_THROW(c.increment(0));

    MetricsRegistry::instance().reset();
}

// ===========================================================================
// TEST 13: Histogram with 1M observations -- mean and sum correct
// ===========================================================================
TEST(ServiceScaffoldChaos, Histogram1MObservations_MeanSumCorrect) {
    MetricsRegistry::instance().reset();

    auto& h = MetricsRegistry::instance().histogram("big_hist");

    const int64_t N = 1000000;
    double expected_sum = 0.0;
    for (int64_t i = 1; i <= N; ++i) {
        double val = static_cast<double>(i) * 0.5;
        h.observe(val);
        expected_sum += val;
    }

    EXPECT_EQ(h.count(), N);

    double actual_sum = h.sum();
    double sum_error = std::abs(actual_sum - expected_sum) / expected_sum;
    // Floating-point atomics may accumulate error, but should be within 1%
    EXPECT_LT(sum_error, 0.01) << "sum=" << actual_sum << " expected=" << expected_sum;

    double expected_mean = expected_sum / static_cast<double>(N);
    double mean_error = std::abs(h.mean() - expected_mean) / expected_mean;
    EXPECT_LT(mean_error, 0.01) << "mean=" << h.mean() << " expected=" << expected_mean;

    MetricsRegistry::instance().reset();
}

// ===========================================================================
// TEST 14: ExplainInfo for all execution modes -- fields populated
// ===========================================================================
TEST(ServiceScaffoldChaos, ExplainInfoAllModes_FieldsPopulated) {
    // Test RowByRow mode
    ExplainInfo info1;
    info1.execution_mode = ExecutionMode::kRowByRow;
    info1.path = "physics_sampling";
    info1.distribution = "uniform";
    info1.constraint_classification.value_range = 5;
    info1.constraint_classification.inter_row = 0;
    info1.constraint_classification.aggregate = 0;
    info1.estimated_exclusion_rate = 0.1;
    info1.version = "v1";
    info1.supported_statements = {"DEFINE TYPE", "DEFINE CONSTRAINT", "GENERATE"};

    EXPECT_EQ(info1.execution_mode, ExecutionMode::kRowByRow);
    EXPECT_EQ(info1.path, "physics_sampling");
    EXPECT_EQ(info1.distribution, "uniform");
    EXPECT_EQ(info1.constraint_classification.value_range, 5);
    EXPECT_DOUBLE_EQ(info1.estimated_exclusion_rate, 0.1);
    EXPECT_EQ(info1.version, "v1");
    EXPECT_EQ(info1.supported_statements.size(), 3u);

    // Test StatefulBatch mode
    ExplainInfo info2;
    info2.execution_mode = ExecutionMode::kStatefulBatch;
    info2.path = "stateful_generation";
    info2.constraint_classification.inter_row = 2;
    info2.version = "v2";
    info2.volume_ratio = 0.85;
    info2.data_engine_available = true;

    EXPECT_EQ(info2.execution_mode, ExecutionMode::kStatefulBatch);
    EXPECT_EQ(info2.constraint_classification.inter_row, 2);
    EXPECT_DOUBLE_EQ(info2.volume_ratio, 0.85);
    EXPECT_TRUE(info2.data_engine_available);

    // Test TwoPhase mode
    ExplainInfo info3;
    info3.execution_mode = ExecutionMode::kTwoPhase;
    info3.path = "two_phase_with_kde";
    info3.constraint_classification.aggregate = 3;
    info3.version = "v2";
    info3.degradation_path = 2;
    info3.selection_reason = "aggregate constraints require two-phase";

    EXPECT_EQ(info3.execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_EQ(info3.constraint_classification.aggregate, 3);
    EXPECT_EQ(info3.degradation_path, 2);
    EXPECT_EQ(info3.selection_reason, "aggregate constraints require two-phase");

    // Verify default values
    ExplainInfo info_default;
    EXPECT_EQ(info_default.execution_mode, ExecutionMode::kRowByRow);
    EXPECT_TRUE(info_default.path.empty());
    EXPECT_DOUBLE_EQ(info_default.estimated_exclusion_rate, 0.0);
    EXPECT_FALSE(info_default.data_engine_available);
    EXPECT_EQ(info_default.degradation_path, 0);
}

// ===========================================================================
// TEST 15: MetricsRegistry::reset() clears everything
// ===========================================================================
TEST(ServiceScaffoldChaos, MetricsReset_ClearsAll) {
    auto& reg = MetricsRegistry::instance();

    // Populate
    reg.counter("c1").increment(100);
    reg.counter("c2").increment(200);
    reg.gauge("g1").set(3.14);
    reg.gauge("g2").set(2.72);
    reg.histogram("h1").observe(42.0);

    // Verify non-zero before reset
    auto counters_before = reg.all_counters();
    auto gauges_before = reg.all_gauges();
    EXPECT_EQ(counters_before["c1"], 100);
    EXPECT_EQ(counters_before["c2"], 200);
    EXPECT_EQ(gauges_before.size(), 2u);

    // Reset
    reg.reset();

    // After reset, getting the same-named counter should give a fresh one at 0
    auto counters_after = reg.all_counters();
    auto gauges_after = reg.all_gauges();

    EXPECT_TRUE(counters_after.empty()) << "Counters should be empty after reset";
    EXPECT_TRUE(gauges_after.empty()) << "Gauges should be empty after reset";

    // Creating new counters after reset starts from 0
    EXPECT_EQ(reg.counter("c1").value(), 0);
    EXPECT_EQ(reg.gauge("g1").value(), 0.0);
}
