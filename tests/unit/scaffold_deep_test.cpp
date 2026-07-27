// Deep tests for the scaffold module: Explain, Trace, Metrics, test_helpers
// Exercises edge cases, thread safety, deep nesting, boundary values, and concurrency.
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <future>
#include <algorithm>
#include <numeric>
#include <random>
#include <unordered_set>
#include <sstream>
#include <functional>
#include <climits>

#include "scaffold/explain.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"
#include "scaffold/test_helpers.h"

using namespace synthgen::scaffold;

// ============================================================================
// Explain Deep Tests
// ============================================================================

class ExplainDeepTest : public ::testing::Test {
protected:
    void SetUp() override {
        MetricsRegistry::instance().reset();
        SpanGuard::active_spans().clear();
    }
    void TearDown() override {
        MetricsRegistry::instance().reset();
        SpanGuard::active_spans().clear();
    }
};

TEST_F(ExplainDeepTest, AllExecutionModes) {
    ExplainInfo info;
    // Default
    EXPECT_EQ(info.execution_mode, ExecutionMode::kRowByRow);

    // Cycle through all modes
    info.execution_mode = ExecutionMode::kStatefulBatch;
    EXPECT_EQ(info.execution_mode, ExecutionMode::kStatefulBatch);

    info.execution_mode = ExecutionMode::kTwoPhase;
    EXPECT_EQ(info.execution_mode, ExecutionMode::kTwoPhase);

    info.execution_mode = ExecutionMode::kRowByRow;
    EXPECT_EQ(info.execution_mode, ExecutionMode::kRowByRow);
}

TEST_F(ExplainDeepTest, AllFieldsV2) {
    ExplainInfo info;
    info.execution_mode = ExecutionMode::kTwoPhase;
    info.path = "two_phase_sampling";
    info.constraint_classification.value_range = 3;
    info.constraint_classification.inter_row = 2;
    info.constraint_classification.aggregate = 1;
    info.distribution = "kde";
    info.estimated_exclusion_rate = 0.15;
    info.supported_statements = {"DEFINE TYPE", "DEFINE CONSTRAINT"};
    info.unsupported_in_v1 = {"DURING", "WHEN"};
    info.version = "v2";
    info.volume_ratio = 1.5;
    info.data_source = "user_parquet";
    info.degradation_path = 2;
    info.selection_reason = "high inter-row constraint count";
    info.data_engine_available = true;

    EXPECT_EQ(info.execution_mode, ExecutionMode::kTwoPhase);
    EXPECT_EQ(info.path, "two_phase_sampling");
    EXPECT_EQ(info.constraint_classification.value_range, 3);
    EXPECT_EQ(info.constraint_classification.inter_row, 2);
    EXPECT_EQ(info.constraint_classification.aggregate, 1);
    EXPECT_EQ(info.distribution, "kde");
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, 0.15);
    EXPECT_EQ(info.version, "v2");
    EXPECT_DOUBLE_EQ(info.volume_ratio, 1.5);
    EXPECT_EQ(info.data_source, "user_parquet");
    EXPECT_EQ(info.degradation_path, 2);
    EXPECT_EQ(info.selection_reason, "high inter-row constraint count");
    EXPECT_TRUE(info.data_engine_available);
}

TEST_F(ExplainDeepTest, ConstraintClassificationDefault) {
    ConstraintClassification cc;
    EXPECT_EQ(cc.value_range, 0);
    EXPECT_EQ(cc.inter_row, 0);
    EXPECT_EQ(cc.aggregate, 0);
}

TEST_F(ExplainDeepTest, ExplainInfoDefaultValues) {
    ExplainInfo info;
    EXPECT_EQ(info.execution_mode, ExecutionMode::kRowByRow);
    EXPECT_TRUE(info.path.empty());
    EXPECT_EQ(info.constraint_classification.value_range, 0);
    EXPECT_EQ(info.constraint_classification.inter_row, 0);
    EXPECT_EQ(info.constraint_classification.aggregate, 0);
    EXPECT_TRUE(info.distribution.empty());
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, 0.0);
    EXPECT_TRUE(info.supported_statements.empty());
    EXPECT_TRUE(info.unsupported_in_v1.empty());
    EXPECT_EQ(info.version, "v1");
    EXPECT_DOUBLE_EQ(info.volume_ratio, 0.0);
    EXPECT_TRUE(info.data_source.empty());
    EXPECT_EQ(info.degradation_path, 0);
    EXPECT_TRUE(info.selection_reason.empty());
    EXPECT_FALSE(info.data_engine_available);
}

TEST_F(ExplainDeepTest, VeryLongStrings) {
    ExplainInfo info;
    std::string long_path(10000, 'a');
    info.path = long_path;
    EXPECT_EQ(info.path.size(), 10000u);

    std::string long_dist(5000, 'x');
    info.distribution = long_dist;
    EXPECT_EQ(info.distribution.size(), 5000u);

    std::string long_reason(20000, 'r');
    info.selection_reason = long_reason;
    EXPECT_EQ(info.selection_reason.size(), 20000u);
}

TEST_F(ExplainDeepTest, UnicodeStrings) {
    ExplainInfo info;
    info.path = u8"/data/\xe4\xb8\xad\xe6\x96\x87/\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88";
    info.distribution = u8"\xf0\x9f\x94\xa5uniform";
    info.selection_reason = u8"\xc3\xa9l\xc3\xa8ve sample";

    EXPECT_FALSE(info.path.empty());
    EXPECT_FALSE(info.distribution.empty());
    EXPECT_FALSE(info.selection_reason.empty());
}

TEST_F(ExplainDeepTest, EmptyStringFields) {
    ExplainInfo info;
    info.path = "";
    info.distribution = "";
    info.data_source = "";
    info.selection_reason = "";

    EXPECT_TRUE(info.path.empty());
    EXPECT_TRUE(info.distribution.empty());
    EXPECT_TRUE(info.data_source.empty());
    EXPECT_TRUE(info.selection_reason.empty());
}

TEST_F(ExplainDeepTest, LargeVectorFields) {
    ExplainInfo info;
    for (int i = 0; i < 10000; ++i) {
        info.supported_statements.push_back("stmt_" + std::to_string(i));
        info.unsupported_in_v1.push_back("unsup_" + std::to_string(i));
    }
    EXPECT_EQ(info.supported_statements.size(), 10000u);
    EXPECT_EQ(info.unsupported_in_v1.size(), 10000u);
    EXPECT_EQ(info.supported_statements[0], "stmt_0");
    EXPECT_EQ(info.supported_statements[9999], "stmt_9999");
}

TEST_F(ExplainDeepTest, NegativeExclusionRate) {
    // Edge: negative exclusion rate (shouldn't happen logically but struct allows it)
    ExplainInfo info;
    info.estimated_exclusion_rate = -0.5;
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, -0.5);
}

TEST_F(ExplainDeepTest, ExtremeExclusionRate) {
    ExplainInfo info;
    info.estimated_exclusion_rate = 1e18;
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, 1e18);

    info.estimated_exclusion_rate = -1e18;
    EXPECT_DOUBLE_EQ(info.estimated_exclusion_rate, -1e18);
}

TEST_F(ExplainDeepTest, MultipleExplainInfos) {
    // Create several ExplainInfo structs with different values
    ExplainInfo a, b, c;
    a.execution_mode = ExecutionMode::kRowByRow;
    a.version = "v1";
    b.execution_mode = ExecutionMode::kStatefulBatch;
    b.version = "v2";
    c.execution_mode = ExecutionMode::kTwoPhase;
    c.version = "v3";

    EXPECT_NE(a.execution_mode, b.execution_mode);
    EXPECT_NE(b.execution_mode, c.execution_mode);
    EXPECT_NE(a.version, c.version);
}

TEST_F(ExplainDeepTest, CopyAndAssign) {
    ExplainInfo original;
    original.execution_mode = ExecutionMode::kTwoPhase;
    original.path = "test_path";
    original.estimated_exclusion_rate = 0.75;
    original.supported_statements = {"A", "B"};
    original.data_engine_available = true;

    ExplainInfo copy = original;
    EXPECT_EQ(copy.execution_mode, original.execution_mode);
    EXPECT_EQ(copy.path, original.path);
    EXPECT_DOUBLE_EQ(copy.estimated_exclusion_rate, original.estimated_exclusion_rate);
    EXPECT_EQ(copy.supported_statements, original.supported_statements);
    EXPECT_EQ(copy.data_engine_available, original.data_engine_available);

    ExplainInfo assigned;
    assigned = original;
    EXPECT_EQ(assigned.execution_mode, original.execution_mode);
    EXPECT_EQ(assigned.path, original.path);
}

TEST_F(ExplainDeepTest, NegativeDegradationPath) {
    ExplainInfo info;
    info.degradation_path = -1;
    EXPECT_EQ(info.degradation_path, -1);
}

TEST_F(ExplainDeepTest, LargeDegradationPath) {
    ExplainInfo info;
    info.degradation_path = INT_MAX;
    EXPECT_EQ(info.degradation_path, INT_MAX);
}

// ============================================================================
// Trace Deep Tests
// ============================================================================

class TraceDeepTest : public ::testing::Test {
protected:
    void SetUp() override {
        SpanGuard::active_spans().clear();
    }
    void TearDown() override {
        SpanGuard::active_spans().clear();
    }
};

TEST_F(TraceDeepTest, SpanTimingIsReasonable) {
    auto before = std::chrono::steady_clock::now();
    {
        SpanGuard guard("test", "timing", "t-timing");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto after = std::chrono::steady_clock::now();

    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    int64_t duration_us = spans[0].end_time_us - spans[0].start_time_us;
    // Duration should be at least 10ms = 10000us
    EXPECT_GE(duration_us, 9000);  // Allow small timing margin
    // Should be less than 1 second
    EXPECT_LT(duration_us, 1000000);
}

TEST_F(TraceDeepTest, SpanTimingZeroDuration) {
    // A span that completes instantly should have start <= end
    {
        SpanGuard guard("instant", "noop", "t-instant");
        // Immediately exits scope
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_GE(spans[0].end_time_us, spans[0].start_time_us);
}

TEST_F(TraceDeepTest, DeepNesting100Levels) {
    SpanGuard::active_spans().clear();
    {
        std::vector<std::unique_ptr<SpanGuard>> guards;
        guards.reserve(100);
        for (int i = 0; i < 100; ++i) {
            std::string parent_id = (i == 0) ? "" : guards.back()->span().span_id;
            guards.push_back(std::make_unique<SpanGuard>(
                "level_" + std::to_string(i),
                "op_" + std::to_string(i),
                "deep-trace",
                parent_id));
        }
        // Destroy in reverse order
        for (int i = 99; i >= 0; --i) {
            guards[i].reset();
        }
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 100u);
    // First destroyed should be last created (level 99)
    EXPECT_EQ(spans[0].component, "level_99");
    // Last destroyed should be first created (level 0)
    EXPECT_EQ(spans[99].component, "level_0");
}

TEST_F(TraceDeepTest, DeepNestingParentChain) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard root("root", "root_op", "chain-trace");
        std::string root_id = root.span().span_id;
        EXPECT_TRUE(root.span().parent_span_id.empty());

        SpanGuard child1("child1", "op1", "chain-trace", root_id);
        std::string child1_id = child1.span().span_id;
        EXPECT_EQ(child1.span().parent_span_id, root_id);

        SpanGuard child2("child2", "op2", "chain-trace", child1_id);
        EXPECT_EQ(child2.span().parent_span_id, child1_id);
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 3u);
}

TEST_F(TraceDeepTest, SpanWithEmptyName) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("", "", "empty-name-trace");
        guard.set_attribute("test", "value");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_TRUE(spans[0].component.empty());
    EXPECT_TRUE(spans[0].operation.empty());
    EXPECT_EQ(spans[0].attributes.at("test"), "value");
}

TEST_F(TraceDeepTest, SpanWithSpecialCharacters) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("comp/with:special.chars",
                        "op with spaces and\ttabs",
                        "trace-with-\"quotes\"");
        guard.set_attribute("key with spaces", "value\nwith\nnewlines");
        guard.set_attribute("key=with&special", "value<with>html");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].component, "comp/with:special.chars");
    EXPECT_EQ(spans[0].operation, "op with spaces and\ttabs");
    EXPECT_EQ(spans[0].attributes.at("key with spaces"), "value\nwith\nnewlines");
}

TEST_F(TraceDeepTest, SpanWithUnicode) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard(u8"\xe4\xb8\xad\xe6\x96\x87",
                        u8"\xe6\x93\x8d\xe4\xbd\x9c",
                        u8"\xe8\xb7\x9f\xe8\xb8\xaa");
        guard.set_attribute(u8"\xe9\x94\xae", u8"\xe5\x80\xbc");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_FALSE(spans[0].component.empty());
    EXPECT_FALSE(spans[0].attributes.empty());
}

TEST_F(TraceDeepTest, SpanWithErrorMessage) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("engine", "generate", "err-trace");
        guard.set_status("error");
        guard.set_attribute("error_message", "out of memory");
        guard.set_attribute("error_code", "500");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].status, "error");
    EXPECT_EQ(spans[0].attributes.at("error_message"), "out of memory");
}

TEST_F(TraceDeepTest, SpanStatusCustomValues) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard g1("test", "op1", "status-trace");
        g1.set_status("cancelled");
    }
    {
        SpanGuard g2("test", "op2", "status-trace");
        g2.set_status("timeout");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].status, "cancelled");
    EXPECT_EQ(spans[1].status, "timeout");
}

TEST_F(TraceDeepTest, SpanStatusOverwrite) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "overwrite", "t-ow");
        guard.set_status("ok");
        guard.set_status("error");
        guard.set_status("ok");  // overwrite back
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].status, "ok");
}

TEST_F(TraceDeepTest, SpanAttributeOverwrite) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "attr-ow", "t-aow");
        guard.set_attribute("key", "first");
        guard.set_attribute("key", "second");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].attributes.at("key"), "second");
}

TEST_F(TraceDeepTest, ManyAttributes) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "many-attrs", "t-many");
        for (int i = 0; i < 1000; ++i) {
            guard.set_attribute("key_" + std::to_string(i),
                                "value_" + std::to_string(i));
        }
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].attributes.size(), 1000u);
    EXPECT_EQ(spans[0].attributes.at("key_0"), "value_0");
    EXPECT_EQ(spans[0].attributes.at("key_999"), "value_999");
}

TEST_F(TraceDeepTest, SpanIdsAreUnique) {
    SpanGuard::active_spans().clear();
    std::vector<std::string> span_ids;
    {
        for (int i = 0; i < 200; ++i) {
            SpanGuard guard("test", "unique_" + std::to_string(i), "unique-trace");
            span_ids.push_back(guard.span().span_id);
        }
    }
    // Check uniqueness
    std::unordered_set<std::string> unique_ids(span_ids.begin(), span_ids.end());
    EXPECT_EQ(unique_ids.size(), span_ids.size())
        << "Duplicate span IDs detected";
}

TEST_F(TraceDeepTest, ConcurrentTracesDifferentThreads) {
    // Each thread should have its own thread_local active_spans
    SpanGuard::active_spans().clear();

    const int num_threads = 8;
    const int spans_per_thread = 10;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, spans_per_thread]() {
            for (int i = 0; i < spans_per_thread; ++i) {
                SpanGuard guard("thread_" + std::to_string(t),
                                "op_" + std::to_string(i),
                                "trace_" + std::to_string(t));
                guard.set_attribute("iteration", std::to_string(i));
            }
            // Verify thread-local spans are correct
            auto& spans = SpanGuard::active_spans();
            EXPECT_EQ(spans.size(), static_cast<size_t>(spans_per_thread));
            for (auto& s : spans) {
                EXPECT_EQ(s.component, "thread_" + std::to_string(t));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Main thread's active_spans should still be empty (we cleared it)
    // (Each thread has its own thread_local vector)
    EXPECT_TRUE(SpanGuard::active_spans().empty())
        << "Main thread spans should be empty after concurrent trace test";
}

TEST_F(TraceDeepTest, TraceSpanDefaultStatus) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "default-status", "t-ds");
        // Don't set status
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].status, "ok");
}

TEST_F(TraceDeepTest, TraceSpanStructDirectAccess) {
    TraceSpan span;
    EXPECT_TRUE(span.trace_id.empty());
    EXPECT_TRUE(span.span_id.empty());
    EXPECT_TRUE(span.parent_span_id.empty());
    EXPECT_TRUE(span.component.empty());
    EXPECT_TRUE(span.operation.empty());
    EXPECT_EQ(span.start_time_us, 0);
    EXPECT_EQ(span.end_time_us, 0);
    EXPECT_EQ(span.status, "ok");
    EXPECT_TRUE(span.attributes.empty());
}

TEST_F(TraceDeepTest, MultipleSpansDifferentTraces) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard g1("a", "op1", "trace-A");
        SpanGuard g2("b", "op2", "trace-B");
        SpanGuard g3("c", "op3", "trace-A");
    }
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 3u);
    // Verify different trace IDs are preserved
    int trace_a_count = 0, trace_b_count = 0;
    for (auto& s : spans) {
        if (s.trace_id == "trace-A") trace_a_count++;
        if (s.trace_id == "trace-B") trace_b_count++;
    }
    EXPECT_EQ(trace_a_count, 2);
    EXPECT_EQ(trace_b_count, 1);
}

TEST_F(TraceDeepTest, ClearActiveSpansBetweenTests) {
    SpanGuard::active_spans().clear();
    {
        SpanGuard guard("test", "op", "t-clear");
    }
    EXPECT_EQ(SpanGuard::active_spans().size(), 1u);
    SpanGuard::active_spans().clear();
    EXPECT_TRUE(SpanGuard::active_spans().empty());
}

// ============================================================================
// Metrics Deep Tests
// ============================================================================

class MetricsDeepTest : public ::testing::Test {
protected:
    void SetUp() override {
        MetricsRegistry::instance().reset();
    }
    void TearDown() override {
        MetricsRegistry::instance().reset();
    }
};

// --- Counter Deep Tests ---

TEST_F(MetricsDeepTest, CounterNegativeIncrement) {
    auto& c = MetricsRegistry::instance().counter("neg_counter");
    c.increment(10);
    EXPECT_EQ(c.value(), 10);
    c.increment(-3);
    EXPECT_EQ(c.value(), 7);
    c.increment(-10);
    EXPECT_EQ(c.value(), -3);
}

TEST_F(MetricsDeepTest, CounterLargeValues) {
    auto& c = MetricsRegistry::instance().counter("large_counter");
    c.increment(INT64_MAX - 100);
    EXPECT_EQ(c.value(), INT64_MAX - 100);
    // Adding more should overflow (undefined behavior, but test the current behavior)
    // Just verify it doesn't crash
    c.increment(50);
    // After overflow: value wraps. This is a known edge case.
}

TEST_F(MetricsDeepTest, CounterZeroIncrement) {
    auto& c = MetricsRegistry::instance().counter("zero_counter");
    c.increment(0);
    EXPECT_EQ(c.value(), 0);
    c.increment(5);
    c.increment(0);
    EXPECT_EQ(c.value(), 5);
}

TEST_F(MetricsDeepTest, CounterDefaultDeltaIsOne) {
    auto& c = MetricsRegistry::instance().counter("default_delta");
    c.increment();  // default delta = 1
    EXPECT_EQ(c.value(), 1);
}

TEST_F(MetricsDeepTest, CounterManyIncrements) {
    auto& c = MetricsRegistry::instance().counter("many_counter");
    for (int i = 0; i < 10000; ++i) {
        c.increment(1);
    }
    EXPECT_EQ(c.value(), 10000);
}

// --- Gauge Deep Tests ---

TEST_F(MetricsDeepTest, GaugeNegativeValue) {
    auto& g = MetricsRegistry::instance().gauge("neg_gauge");
    g.set(-100.5);
    EXPECT_DOUBLE_EQ(g.value(), -100.5);
}

TEST_F(MetricsDeepTest, GaugeVeryLargeValue) {
    auto& g = MetricsRegistry::instance().gauge("large_gauge");
    g.set(1e300);
    EXPECT_DOUBLE_EQ(g.value(), 1e300);
}

TEST_F(MetricsDeepTest, GaugeVerySmallValue) {
    auto& g = MetricsRegistry::instance().gauge("small_gauge");
    g.set(1e-300);
    EXPECT_DOUBLE_EQ(g.value(), 1e-300);
}

TEST_F(MetricsDeepTest, GaugeZeroValue) {
    auto& g = MetricsRegistry::instance().gauge("zero_gauge");
    g.set(42.0);
    g.set(0.0);
    EXPECT_DOUBLE_EQ(g.value(), 0.0);
}

TEST_F(MetricsDeepTest, GaugeOverwrite) {
    auto& g = MetricsRegistry::instance().gauge("overwrite_gauge");
    g.set(1.0);
    g.set(2.0);
    g.set(3.0);
    EXPECT_DOUBLE_EQ(g.value(), 3.0);
}

// --- Histogram Deep Tests ---

TEST_F(MetricsDeepTest, HistogramNegativeValues) {
    auto& h = MetricsRegistry::instance().histogram("neg_hist");
    h.observe(-10.0);
    h.observe(-20.0);
    h.observe(-30.0);
    EXPECT_EQ(h.count(), 3);
    EXPECT_DOUBLE_EQ(h.sum(), -60.0);
    EXPECT_DOUBLE_EQ(h.mean(), -20.0);
}

TEST_F(MetricsDeepTest, HistogramSingleValue) {
    auto& h = MetricsRegistry::instance().histogram("single_hist");
    h.observe(42.0);
    EXPECT_EQ(h.count(), 1);
    EXPECT_DOUBLE_EQ(h.sum(), 42.0);
    EXPECT_DOUBLE_EQ(h.mean(), 42.0);
}

TEST_F(MetricsDeepTest, HistogramMixedValues) {
    auto& h = MetricsRegistry::instance().histogram("mixed_hist");
    h.observe(-100.0);
    h.observe(0.0);
    h.observe(100.0);
    EXPECT_EQ(h.count(), 3);
    EXPECT_DOUBLE_EQ(h.sum(), 0.0);
    EXPECT_DOUBLE_EQ(h.mean(), 0.0);
}

TEST_F(MetricsDeepTest, HistogramManyObservations) {
    auto& h = MetricsRegistry::instance().histogram("many_hist");
    double expected_sum = 0.0;
    for (int i = 1; i <= 1000; ++i) {
        h.observe(static_cast<double>(i));
        expected_sum += i;
    }
    EXPECT_EQ(h.count(), 1000);
    EXPECT_DOUBLE_EQ(h.sum(), expected_sum);
    EXPECT_DOUBLE_EQ(h.mean(), expected_sum / 1000.0);
}

TEST_F(MetricsDeepTest, HistogramVeryLargeValue) {
    auto& h = MetricsRegistry::instance().histogram("vlarge_hist");
    h.observe(1e300);
    EXPECT_EQ(h.count(), 1);
    EXPECT_DOUBLE_EQ(h.sum(), 1e300);
}

// --- MetricsRegistry Deep Tests ---

TEST_F(MetricsDeepTest, SameCounterReturnsSameReference) {
    auto& c1 = MetricsRegistry::instance().counter("same_c");
    auto& c2 = MetricsRegistry::instance().counter("same_c");
    c1.increment(42);
    EXPECT_EQ(c2.value(), 42);
    EXPECT_EQ(&c1, &c2);
}

TEST_F(MetricsDeepTest, SameGaugeReturnsSameReference) {
    auto& g1 = MetricsRegistry::instance().gauge("same_g");
    auto& g2 = MetricsRegistry::instance().gauge("same_g");
    g1.set(99.9);
    EXPECT_DOUBLE_EQ(g2.value(), 99.9);
    EXPECT_EQ(&g1, &g2);
}

TEST_F(MetricsDeepTest, SameHistogramReturnsSameReference) {
    auto& h1 = MetricsRegistry::instance().histogram("same_h");
    auto& h2 = MetricsRegistry::instance().histogram("same_h");
    h1.observe(10.0);
    EXPECT_EQ(h2.count(), 1);
    EXPECT_EQ(&h1, &h2);
}

TEST_F(MetricsDeepTest, DifferentCountersAreIndependent) {
    auto& c1 = MetricsRegistry::instance().counter("c_a");
    auto& c2 = MetricsRegistry::instance().counter("c_b");
    c1.increment(100);
    c2.increment(200);
    EXPECT_EQ(c1.value(), 100);
    EXPECT_EQ(c2.value(), 200);
}

TEST_F(MetricsDeepTest, AllCountersSnapshot) {
    MetricsRegistry::instance().counter("m1").increment(10);
    MetricsRegistry::instance().counter("m2").increment(20);
    MetricsRegistry::instance().counter("m3").increment(30);
    auto all = MetricsRegistry::instance().all_counters();
    EXPECT_EQ(all.size(), 3u);
    EXPECT_EQ(all["m1"], 10);
    EXPECT_EQ(all["m2"], 20);
    EXPECT_EQ(all["m3"], 30);
}

TEST_F(MetricsDeepTest, AllGaugesSnapshot) {
    MetricsRegistry::instance().gauge("g1").set(1.1);
    MetricsRegistry::instance().gauge("g2").set(2.2);
    auto all = MetricsRegistry::instance().all_gauges();
    EXPECT_EQ(all.size(), 2u);
    EXPECT_DOUBLE_EQ(all["g1"], 1.1);
    EXPECT_DOUBLE_EQ(all["g2"], 2.2);
}

TEST_F(MetricsDeepTest, ResetClearsEverything) {
    MetricsRegistry::instance().counter("rc").increment(100);
    MetricsRegistry::instance().gauge("rg").set(99.0);
    MetricsRegistry::instance().histogram("rh").observe(5.0);

    MetricsRegistry::instance().reset();

    EXPECT_TRUE(MetricsRegistry::instance().all_counters().empty());
    EXPECT_TRUE(MetricsRegistry::instance().all_gauges().empty());
}

TEST_F(MetricsDeepTest, ResetAllowsReRegistration) {
    MetricsRegistry::instance().counter("re_reg").increment(50);
    MetricsRegistry::instance().reset();
    auto& c = MetricsRegistry::instance().counter("re_reg");
    EXPECT_EQ(c.value(), 0);  // fresh counter after reset
}

TEST_F(MetricsDeepTest, CounterNamesPreserved) {
    MetricsRegistry::instance().counter("alpha");
    MetricsRegistry::instance().counter("beta");
    MetricsRegistry::instance().counter("gamma");
    auto& a = MetricsRegistry::instance().counter("alpha");
    EXPECT_EQ(a.name(), "alpha");
    auto& b = MetricsRegistry::instance().counter("beta");
    EXPECT_EQ(b.name(), "beta");
}

TEST_F(MetricsDeepTest, GaugeNamesPreserved) {
    auto& g = MetricsRegistry::instance().gauge("my_gauge");
    EXPECT_EQ(g.name(), "my_gauge");
}

TEST_F(MetricsDeepTest, HistogramNamesPreserved) {
    auto& h = MetricsRegistry::instance().histogram("my_hist");
    EXPECT_EQ(h.name(), "my_hist");
}

TEST_F(MetricsDeepTest, EmptyNamesWork) {
    auto& c = MetricsRegistry::instance().counter("");
    c.increment(1);
    EXPECT_EQ(c.value(), 1);
    EXPECT_EQ(c.name(), "");
}

TEST_F(MetricsDeepTest, ConcurrentCounterIncrements) {
    auto& c = MetricsRegistry::instance().counter("concurrent_c");
    const int num_threads = 8;
    const int increments_per_thread = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&c, increments_per_thread]() {
            for (int i = 0; i < increments_per_thread; ++i) {
                c.increment(1);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(c.value(), num_threads * increments_per_thread);
}

TEST_F(MetricsDeepTest, ConcurrentGaugeSets) {
    auto& g = MetricsRegistry::instance().gauge("concurrent_g");
    const int num_threads = 8;
    std::vector<std::thread> threads;
    std::atomic<double> final_values{0.0};

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&g, t]() {
            g.set(static_cast<double>(t));
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Value should be one of [0..num_threads-1]
    double val = g.value();
    EXPECT_GE(val, 0.0);
    EXPECT_LT(val, static_cast<double>(num_threads));
}

TEST_F(MetricsDeepTest, ConcurrentHistogramObservations) {
    auto& h = MetricsRegistry::instance().histogram("concurrent_h");
    const int num_threads = 8;
    const int obs_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&h, obs_per_thread]() {
            for (int i = 0; i < obs_per_thread; ++i) {
                h.observe(1.0);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(h.count(), num_threads * obs_per_thread);
    EXPECT_DOUBLE_EQ(h.sum(), static_cast<double>(num_threads * obs_per_thread));
    EXPECT_DOUBLE_EQ(h.mean(), 1.0);
}

TEST_F(MetricsDeepTest, ConcurrentCounterAndRegistry) {
    // Test concurrent counter creation and increment
    const int num_threads = 4;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t]() {
            std::string name = "thread_counter_" + std::to_string(t);
            auto& c = MetricsRegistry::instance().counter(name);
            c.increment(t * 100);
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Verify all counters exist
    auto all = MetricsRegistry::instance().all_counters();
    EXPECT_EQ(all.size(), static_cast<size_t>(num_threads));
    for (int t = 0; t < num_threads; ++t) {
        std::string name = "thread_counter_" + std::to_string(t);
        EXPECT_EQ(all[name], t * 100);
    }
}

// ============================================================================
// Combined Scaffold Tests (cross-module interactions)
// ============================================================================

class ScaffoldCombinedTest : public ::testing::Test {
protected:
    void SetUp() override {
        MetricsRegistry::instance().reset();
        SpanGuard::active_spans().clear();
    }
    void TearDown() override {
        MetricsRegistry::instance().reset();
        SpanGuard::active_spans().clear();
    }
};

TEST_F(ScaffoldCombinedTest, SpanWithMetricsAndExplain) {
    // Typical usage pattern: span + metrics + explain together
    ExplainInfo info;
    info.execution_mode = ExecutionMode::kRowByRow;
    info.path = "physics_sampling";

    auto& gen_counter = MetricsRegistry::instance().counter("generate_total");
    auto& error_gauge = MetricsRegistry::instance().gauge("error_rate");
    auto& latency_hist = MetricsRegistry::instance().histogram("generation_latency_ms");

    {
        SpanGuard span("engine", "generate", "combined-trace");
        span.set_attribute("mode", "row_by_row");

        gen_counter.increment();
        latency_hist.observe(5.0);
        error_gauge.set(0.01);

        span.set_attribute("rows_generated", "100");
        span.set_status("ok");
    }

    EXPECT_EQ(gen_counter.value(), 1);
    EXPECT_DOUBLE_EQ(error_gauge.value(), 0.01);
    EXPECT_EQ(latency_hist.count(), 1);

    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].status, "ok");
    EXPECT_EQ(spans[0].attributes.at("mode"), "row_by_row");
}

TEST_F(ScaffoldCombinedTest, ErrorPath) {
    // Simulate an error path through the system
    auto& error_counter = MetricsRegistry::instance().counter("errors_total");

    {
        SpanGuard span("engine", "generate", "error-trace");
        span.set_attribute("attempt", "1");
        span.set_status("error");
        span.set_attribute("error", "constraint_violation");
        error_counter.increment();
    }

    EXPECT_EQ(error_counter.value(), 1);
    auto& spans = SpanGuard::active_spans();
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].status, "error");
}

TEST_F(ScaffoldCombinedTest, MultipleOperationsInSequence) {
    auto& ops_counter = MetricsRegistry::instance().counter("ops_total");

    for (int i = 0; i < 5; ++i) {
        SpanGuard span("engine", "op_" + std::to_string(i),
                       "seq-trace-" + std::to_string(i));
        span.set_attribute("index", std::to_string(i));
        ops_counter.increment();
    }

    EXPECT_EQ(ops_counter.value(), 5);
    EXPECT_EQ(SpanGuard::active_spans().size(), 5u);
}

TEST_F(ScaffoldCombinedTest, NestedOperationsWithMetrics) {
    auto& outer_counter = MetricsRegistry::instance().counter("outer_ops");
    auto& inner_counter = MetricsRegistry::instance().counter("inner_ops");

    {
        SpanGuard outer("engine", "batch", "nested-trace");
        outer_counter.increment();

        for (int i = 0; i < 3; ++i) {
            SpanGuard inner("physics", "sample_" + std::to_string(i),
                           "nested-trace", outer.span().span_id);
            inner_counter.increment();
            inner.set_attribute("batch_index", std::to_string(i));
        }
    }

    EXPECT_EQ(outer_counter.value(), 1);
    EXPECT_EQ(inner_counter.value(), 3);
    auto& spans = SpanGuard::active_spans();
    EXPECT_EQ(spans.size(), 4u);  // 3 inner + 1 outer
}

// ============================================================================
// SeedFixedTest Deep Tests
// ============================================================================

class SeededTestFixture : public SeedFixedTest {};

TEST_F(SeededTestFixture, SeedIsDeterministicAcrossMultipleFixtures) {
    EXPECT_EQ(seed_, 42u);
    seed_ = 123;
    EXPECT_EQ(seed_, 123u);
}

TEST_F(SeededTestFixture, SeedCanBeUsedForRandomGeneration) {
    std::mt19937 gen(static_cast<std::mt19937::result_type>(seed_));
    std::uniform_int_distribution<int> dist(1, 100);

    // Generate two sequences with same seed - should be identical
    std::vector<int> seq1, seq2;
    for (int i = 0; i < 10; ++i) {
        seq1.push_back(dist(gen));
    }

    // Reset with same seed
    gen.seed(static_cast<std::mt19937::result_type>(seed_));
    for (int i = 0; i < 10; ++i) {
        seq2.push_back(dist(gen));
    }

    EXPECT_EQ(seq1, seq2) << "Same seed should produce identical sequences";
}

// ============================================================================
// Stress / Boundary Tests
// ============================================================================

TEST(ScaffoldStressTest, ManyConcurrentSpans) {
    SpanGuard::active_spans().clear();
    const int num_threads = 16;
    const int spans_per_thread = 100;
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, spans_per_thread, &success_count]() {
            for (int i = 0; i < spans_per_thread; ++i) {
                SpanGuard guard("stress_" + std::to_string(t),
                                "op_" + std::to_string(i),
                                "stress_trace");
                guard.set_attribute("i", std::to_string(i));
            }
            auto& spans = SpanGuard::active_spans();
            if (spans.size() == static_cast<size_t>(spans_per_thread)) {
                success_count.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(success_count.load(), num_threads);
    SpanGuard::active_spans().clear();
}

TEST(ScaffoldStressTest, RapidCreateDestroy) {
    SpanGuard::active_spans().clear();
    for (int i = 0; i < 10000; ++i) {
        SpanGuard guard("rapid", "op_" + std::to_string(i), "rapid-trace");
        // Immediate destruction
    }
    EXPECT_EQ(SpanGuard::active_spans().size(), 10000u);
    SpanGuard::active_spans().clear();
}

TEST(ScaffoldStressTest, CounterStressTest) {
    MetricsRegistry::instance().reset();
    auto& c = MetricsRegistry::instance().counter("stress_counter");
    const int num_threads = 16;
    const int ops_per_thread = 50000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&c, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                c.increment(1);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(c.value(), num_threads * ops_per_thread);
    MetricsRegistry::instance().reset();
}

TEST(ScaffoldStressTest, HistogramStressTest) {
    MetricsRegistry::instance().reset();
    auto& h = MetricsRegistry::instance().histogram("stress_hist");
    const int num_threads = 16;
    const int obs_per_thread = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&h, obs_per_thread]() {
            for (int i = 0; i < obs_per_thread; ++i) {
                h.observe(1.0);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(h.count(), num_threads * obs_per_thread);
    EXPECT_DOUBLE_EQ(h.mean(), 1.0);
    MetricsRegistry::instance().reset();
}
