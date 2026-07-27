#include <gtest/gtest.h>
#include "scaffold/trace.h"
#include "scaffold/explain.h"
#include "scaffold/metrics.h"

using namespace synthgen::scaffold;

// --- Trace Tests ---

TEST(TraceTest, SpanGuardCreatesSpan) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("parser", "parse", "trace-001");
        guard.set_attribute("stmt_count", "3");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].component, "parser");
    EXPECT_EQ(spans[0].operation, "parse");
    EXPECT_EQ(spans[0].trace_id, "trace-001");
    EXPECT_EQ(spans[0].status, "ok");
    EXPECT_GT(spans[0].end_time_us, spans[0].start_time_us);
    EXPECT_EQ(spans[0].attributes.at("stmt_count"), "3");
}

TEST(TraceTest, NestedSpans) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard outer("engine", "generate", "t-1");
        {
            SpanGuard inner("physics", "sample_batch", "t-1", outer.span().span_id);
            inner.set_attribute("batch_index", "0");
        }
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 2u);
    // Inner span is pushed first (LIFO destruction), outer second
    // spans[0] = inner (physics), spans[1] = outer (engine)
    // Inner's parent was set to outer's span_id at construction time
    EXPECT_EQ(spans[0].component, "physics");
    EXPECT_EQ(spans[1].component, "engine");
    EXPECT_FALSE(spans[0].parent_span_id.empty());
}

TEST(TraceTest, SpanStatus) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("storage", "append", "t-2");
        guard.set_status("error");
    }
    EXPECT_EQ(SpanGuard::active_spans()[0].status, "error");
}

TEST(TraceTest, MultipleAttributes) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "op", "t-3");
        guard.set_attribute("a", "1");
        guard.set_attribute("b", "2");
    }
    auto& span = SpanGuard::active_spans()[0];
    EXPECT_EQ(span.attributes.size(), 2u);
    EXPECT_EQ(span.attributes.at("a"), "1");
    EXPECT_EQ(span.attributes.at("b"), "2");
}

// --- Explain Tests ---

TEST(ExplainTest, DefaultValues) {
    ExplainInfo info;
    EXPECT_EQ(info.execution_mode, ExecutionMode::kRowByRow);
    EXPECT_EQ(info.version, "v1");
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, 0.0);
}

TEST(ExplainTest, PhysicsExplain) {
    ExplainInfo info;
    info.execution_mode = ExecutionMode::kRowByRow;
    info.path = "physics_sampling";
    info.distribution = "uniform";
    info.constraint_classification.value_range = 2;
    info.estimated_exclusion_rate = 0.0;
    EXPECT_EQ(info.path, "physics_sampling");
    EXPECT_EQ(info.constraint_classification.value_range, 2);
    EXPECT_EQ(info.constraint_classification.inter_row, 0);
}

TEST(ExplainTest, ParserExplain) {
    ExplainInfo info;
    info.supported_statements = {"DEFINE TYPE", "LOAD DATA", "DEFINE CONSTRAINT", "GENERATE TABLE"};
    info.unsupported_in_v1 = {"DURING", "WHEN", "inter-row", "aggregate"};
    EXPECT_EQ(info.supported_statements.size(), 4u);
    EXPECT_EQ(info.unsupported_in_v1.size(), 4u);
}

// --- Metrics Tests ---

TEST(MetricsTest, CounterIncrement) {
    MetricsRegistry::instance().reset();
    auto& c = MetricsRegistry::instance().counter("test_counter");
    EXPECT_EQ(c.value(), 0);
    c.increment();
    EXPECT_EQ(c.value(), 1);
    c.increment(5);
    EXPECT_EQ(c.value(), 6);
}

TEST(MetricsTest, GaugeSet) {
    MetricsRegistry::instance().reset();
    auto& g = MetricsRegistry::instance().gauge("test_gauge");
    g.set(42.5);
    EXPECT_DOUBLE_EQ(g.value(), 42.5);
    g.set(100.0);
    EXPECT_DOUBLE_EQ(g.value(), 100.0);
}

TEST(MetricsTest, HistogramObserve) {
    MetricsRegistry::instance().reset();
    auto& h = MetricsRegistry::instance().histogram("test_hist");
    h.observe(10.0);
    h.observe(20.0);
    h.observe(30.0);
    EXPECT_EQ(h.count(), 3);
    EXPECT_DOUBLE_EQ(h.sum(), 60.0);
    EXPECT_DOUBLE_EQ(h.mean(), 20.0);
}

TEST(MetricsTest, AllCounters) {
    MetricsRegistry::instance().reset();
    MetricsRegistry::instance().counter("c1").increment(10);
    MetricsRegistry::instance().counter("c2").increment(20);
    auto all = MetricsRegistry::instance().all_counters();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(all["c1"], 10);
    EXPECT_EQ(all["c2"], 20);
}

TEST(MetricsTest, Reset) {
    MetricsRegistry::instance().counter("x").increment(100);
    MetricsRegistry::instance().reset();
    auto all = MetricsRegistry::instance().all_counters();
    EXPECT_TRUE(all.empty());
}

TEST(MetricsTest, CounterNames) {
    MetricsRegistry::instance().reset();
    auto& c = MetricsRegistry::instance().counter("generation_total");
    EXPECT_EQ(c.name(), "generation_total");
}

TEST(MetricsTest, SameCounterReturnsSame) {
    MetricsRegistry::instance().reset();
    auto& c1 = MetricsRegistry::instance().counter("same");
    auto& c2 = MetricsRegistry::instance().counter("same");
    c1.increment(5);
    EXPECT_EQ(c2.value(), 5);
}

TEST(MetricsTest, HistogramEmpty) {
    MetricsRegistry::instance().reset();
    auto& h = MetricsRegistry::instance().histogram("empty");
    EXPECT_EQ(h.count(), 0);
    EXPECT_DOUBLE_EQ(h.mean(), 0.0);
}
