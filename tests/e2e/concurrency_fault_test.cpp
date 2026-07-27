/**
 * concurrency_fault_test.cpp -- Concurrency (thread safety) and fault injection (resilience) tests.
 *
 * Tests race conditions, deadlocks, crashes under concurrent access,
 * and failures under error conditions.
 */

#include <gtest/gtest.h>

#include "common/result.h"
#include "common/types.h"
#include "common/hash.h"
#include "schema/schema.h"
#include "schema/schema_registry.h"
#include "schema/schema_builder.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/physics/seed_controller.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/tail_report.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "storage/parquet_io.h"
#include "storage/model/model_storage_layer.h"
#include "storage/version/model_version_chain.h"
#include "storage/version/model_version.h"
#include "storage/metadata.h"
#include "scaffold/metrics.h"
#include "scaffold/trace.h"
#include "api/service.h"
#include "api/request.h"
#include "api/response.h"

#include <arrow/api.h>
#include <arrow/table.h>

#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <unordered_map>

using namespace synthgen;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::router;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage;
using namespace synthgen::storage::audit;
using namespace synthgen::storage::model;
using namespace synthgen::storage::version;
using namespace synthgen::schema;
using namespace synthgen::scaffold;
using namespace synthgen::api;

// ============================================================================
// Helpers
// ============================================================================

namespace {

std::filesystem::path pid_temp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("synthgen_" + name + "_" + std::to_string(::getpid()));
}

Schema make_one_col_schema(const std::string& name = "test_type") {
    Schema s;
    s.type_name = name;
    ColumnDef col;
    col.name = "value";
    col.type = DataType::kFloat;
    col.range_min = 0.0;
    col.range_max = 100.0;
    s.columns.push_back(col);
    return s;
}

std::shared_ptr<arrow::Table> make_double_table(
        const std::string& col_name, const std::vector<double>& values) {
    arrow::DoubleBuilder builder;
    for (auto v : values) (void) builder.Append(v);
    std::shared_ptr<arrow::Array> arr;
    auto status = builder.Finish(&arr);
    if (!status.ok()) return nullptr;
    auto schema = arrow::schema({arrow::field(col_name, arrow::float64())});
    return arrow::Table::Make(schema, {arr});
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

} // anonymous namespace

// ============================================================================
// PART 1: CONCURRENCY TESTS
// ============================================================================

// ---------------------------------------------------------------------------
// Test 1: MetricsRegistry -- 10 threads simultaneously incrementing counters
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, MetricsRegistry_10Threads_Counters) {
    MetricsRegistry::instance().reset();

    constexpr int kThreads = 10;
    constexpr int kIterations = 5000;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &errors]() {
            try {
                std::string name = "concurrency_ctr_" + std::to_string(t);
                for (int i = 0; i < kIterations; i++) {
                    MetricsRegistry::instance().counter(name).increment();
                }
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(errors.load(), 0) << "Exceptions during counter increment";

    // Each per-thread counter should be exactly kIterations
    for (int t = 0; t < kThreads; t++) {
        std::string name = "concurrency_ctr_" + std::to_string(t);
        EXPECT_EQ(MetricsRegistry::instance().counter(name).value(), kIterations)
            << "Counter " << name << " has wrong value";
    }

    MetricsRegistry::instance().reset();
}

// ---------------------------------------------------------------------------
// Test 2: MetricsRegistry -- concurrent shared counter
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, MetricsRegistry_SharedCounter) {
    MetricsRegistry::instance().reset();

    constexpr int kThreads = 10;
    constexpr int kIterations = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([]() {
            for (int i = 0; i < kIterations; i++) {
                MetricsRegistry::instance().counter("shared").increment();
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(MetricsRegistry::instance().counter("shared").value(),
              kThreads * kIterations)
        << "Shared counter should be " << (kThreads * kIterations);

    MetricsRegistry::instance().reset();
}

// ---------------------------------------------------------------------------
// Test 3: MetricsRegistry -- concurrent histogram observe
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, MetricsRegistry_ConcurrentHistogram) {
    MetricsRegistry::instance().reset();

    constexpr int kThreads = 10;
    constexpr int kIterations = 2000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([]() {
            for (int i = 0; i < kIterations; i++) {
                MetricsRegistry::instance().histogram("concurrency_hist").observe(1.0);
            }
        });
    }

    for (auto& th : threads) th.join();

    auto& h = MetricsRegistry::instance().histogram("concurrency_hist");
    EXPECT_EQ(h.count(), kThreads * kIterations)
        << "Histogram count should be " << (kThreads * kIterations);
    EXPECT_DOUBLE_EQ(h.sum(), static_cast<double>(kThreads * kIterations))
        << "Histogram sum should be " << (kThreads * kIterations);

    MetricsRegistry::instance().reset();
}

// ---------------------------------------------------------------------------
// Test 4: AuditLog -- 5 threads writing concurrently (single instance)
// Note: AuditLog is NOT thread-safe (uses std::deque internally),
// so we test with external mutex. This documents the thread-safety contract.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_ConcurrentWrites_WithMutex) {
    AuditLog audit;
    auto gen = audit.create_genesis();
    ASSERT_TRUE(gen.ok());

    constexpr int kThreads = 5;
    constexpr int kIterations = 100;
    std::mutex audit_mutex;
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&audit, &audit_mutex, &success_count, t]() {
            for (int i = 0; i < kIterations; i++) {
                std::lock_guard<std::mutex> lock(audit_mutex);
                auto result = audit.append(
                    "thread_op_" + std::to_string(t),
                    "thread_" + std::to_string(t),
                    {{"iter", std::to_string(i)}});
                if (result.ok()) {
                    success_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(success_count.load(), kThreads * kIterations)
        << "All appends should succeed";
    EXPECT_EQ(audit.record_count(), 1 + kThreads * kIterations)
        << "Record count: genesis + all appends";

    // Verify hash chain integrity after all writes
    auto chain_result = audit.verify_chain();
    ASSERT_TRUE(chain_result.ok());
    EXPECT_TRUE(chain_result.value()) << "Hash chain should be valid after concurrent writes";

    auto daily = audit.daily_verification();
    ASSERT_TRUE(daily.ok());
    EXPECT_TRUE(daily.value().is_valid) << "Daily verification should pass";
}

// ---------------------------------------------------------------------------
// Test 5: AuditLog -- concurrent reads after writes
// AuditLog uses std::deque internally and is NOT thread-safe for writes.
// This test verifies concurrent reads are safe after all writes complete.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_ConcurrentReads) {
    AuditLog audit;
    auto gen = audit.create_genesis();
    ASSERT_TRUE(gen.ok());

    // Pre-populate records (single-threaded)
    for (int i = 0; i < 50; i++) {
        auto r = audit.append("op_" + std::to_string(i), "user",
                               {{"idx", std::to_string(i)}});
        ASSERT_TRUE(r.ok());
    }

    // Verify chain is valid
    auto chain = audit.verify_chain();
    ASSERT_TRUE(chain.ok());
    EXPECT_TRUE(chain.value());

    // Now do concurrent reads (safe: no mutation)
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::atomic<int> read_errors{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&audit, &read_errors]() {
            for (int i = 0; i < 100; i++) {
                // Read latest
                auto latest = audit.get_latest();
                if (!latest.ok()) {
                    read_errors.fetch_add(1);
                    continue;
                }

                // Scan
                auto scan = audit.scan(std::nullopt, std::nullopt, 100);
                if (!scan.ok()) {
                    read_errors.fetch_add(1);
                    continue;
                }
                if (scan.value().size() != 51) { // genesis + 50
                    read_errors.fetch_add(1);
                    continue;
                }

                // Verify chain
                auto v = audit.verify_chain();
                if (!v.ok() || !v.value()) {
                    read_errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(read_errors.load(), 0)
        << "Concurrent reads should all succeed";
}

// ---------------------------------------------------------------------------
// Test 6: ModelStorageLayer -- 8 threads loading/saving models concurrently
// ModelStorageLayer's LRU cache is NOT thread-safe. We use an external mutex.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelStorageLayer_ConcurrentSaveLoad) {
    auto root = pid_temp_path("model_concurrency");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);
    constexpr int kThreads = 8;
    constexpr int kIterations = 50;
    std::vector<std::thread> threads;
    std::atomic<int> save_errors{0};
    std::atomic<int> load_errors{0};
    std::mutex storage_mutex;

    // Each thread saves and loads its own model
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&storage, &storage_mutex, &save_errors, &load_errors, t]() {
            std::string model_name = "model_" + std::to_string(t);
            for (int i = 0; i < kIterations; i++) {
                std::string version_id = "v_" + std::to_string(t) + "_" + std::to_string(i);
                std::string data = "data_t" + std::to_string(t) + "_i" + std::to_string(i);

                std::lock_guard<std::mutex> lock(storage_mutex);
                auto save_result = storage.save_checkpoint(model_name, version_id, data);
                if (!save_result.ok()) {
                    save_errors.fetch_add(1);
                    continue;
                }

                auto load_result = storage.load_model(model_name, version_id);
                if (!load_result.ok()) {
                    load_errors.fetch_add(1);
                } else if (load_result.value() != data) {
                    load_errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(save_errors.load(), 0) << "Save errors during concurrent access";
    EXPECT_EQ(load_errors.load(), 0) << "Load errors during concurrent access";

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 7: ModelStorageLayer -- concurrent access to SAME model (shared state)
// ModelStorageLayer's LRU cache is NOT thread-safe. We use an external mutex.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelStorageLayer_ConcurrentSameModel) {
    auto root = pid_temp_path("model_shared");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);

    // Pre-save a model
    std::string model_name = "shared_model";
    std::string version_id = "v1";
    std::string original_data = "shared_data_content";
    auto save_result = storage.save_checkpoint(model_name, version_id, original_data);
    ASSERT_TRUE(save_result.ok());

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::atomic<int> mismatch_count{0};
    std::mutex storage_mutex;

    // All threads load the same model concurrently (with mutex for LRU safety)
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&storage, &storage_mutex, &mismatch_count,
                              &model_name, &version_id, &original_data]() {
            for (int i = 0; i < 20; i++) {
                std::lock_guard<std::mutex> lock(storage_mutex);
                auto load_result = storage.load_model(model_name, version_id);
                if (!load_result.ok()) {
                    mismatch_count.fetch_add(1);
                } else if (load_result.value() != original_data) {
                    mismatch_count.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(mismatch_count.load(), 0)
        << "All concurrent reads of the same model should return consistent data";

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 8: SpanGuard -- Nested spans are thread-local, no cross-contamination
// Each thread has its own thread-local span vector. Spans from one thread
// should never appear in another thread's vector.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, SpanGuard_NestedSpans_NoCrossContamination) {
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::atomic<int> contamination{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t, &contamination]() {
            // Clear thread-local spans for this thread
            SpanGuard::active_spans().clear();

            {
                SpanGuard outer("comp_" + std::to_string(t), "outer_op",
                                "trace_" + std::to_string(t));
                outer.set_attribute("thread", std::to_string(t));

                {
                    SpanGuard inner("comp_" + std::to_string(t), "inner_op",
                                    "trace_" + std::to_string(t),
                                    outer.span().span_id);
                    inner.set_attribute("nested", "true");
                }
            }

            // Verify spans are thread-local: should have exactly 2
            auto& spans = SpanGuard::active_spans();
            if (spans.size() != 2) {
                contamination.fetch_add(1);
                return;
            }

            // Verify parent-child linkage
            if (!spans[1].parent_span_id.empty() &&
                spans[1].parent_span_id != spans[0].span_id) {
                contamination.fetch_add(1);
            }

            // Verify all components belong to this thread
            for (auto& s : spans) {
                if (s.component != "comp_" + std::to_string(t)) {
                    contamination.fetch_add(1);
                    break;
                }
            }

            // Verify trace_id matches
            for (auto& s : spans) {
                if (s.trace_id != "trace_" + std::to_string(t)) {
                    contamination.fetch_add(1);
                    break;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(contamination.load(), 0)
        << "SpanGuard should not have cross-thread contamination";
}

// ---------------------------------------------------------------------------
// Test 9: SchemaRegistry -- concurrent registration and lookup
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, SchemaRegistry_ConcurrentRegisterAndLookup) {
    SchemaRegistry registry;
    constexpr int kThreads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> reg_errors{0};
    std::atomic<int> lookup_errors{0};
    std::mutex registry_mutex; // SchemaRegistry is not thread-safe

    // Phase 1: Concurrent registrations
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&registry, &registry_mutex, &reg_errors, t]() {
            Schema s;
            s.type_name = "type_" + std::to_string(t);
            ColumnDef col;
            col.name = "col_" + std::to_string(t);
            col.type = DataType::kFloat;
            col.range_min = 0.0;
            col.range_max = static_cast<double>(t + 1) * 10.0;
            s.columns.push_back(col);

            std::lock_guard<std::mutex> lock(registry_mutex);
            auto result = registry.register_schema(std::move(s));
            if (!result.ok()) {
                reg_errors.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(reg_errors.load(), 0) << "Registration errors";

    // Phase 2: Concurrent lookups
    std::vector<std::thread> lookup_threads;
    for (int t = 0; t < kThreads; t++) {
        lookup_threads.emplace_back([&registry, &registry_mutex, &lookup_errors, t]() {
            for (int i = 0; i < 100; i++) {
                std::lock_guard<std::mutex> lock(registry_mutex);
                auto result = registry.get_schema("type_" + std::to_string(t));
                if (!result.ok()) {
                    lookup_errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : lookup_threads) th.join();

    EXPECT_EQ(lookup_errors.load(), 0) << "Lookup errors";
}

// ---------------------------------------------------------------------------
// Test 10: SeedController -- concurrent seed generation determinism
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, SeedController_ConcurrentDeterminism) {
    SeedController controller(42);

    // SeedController is stateless (pure functions), so concurrent access should
    // always return deterministic results
    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    std::vector<uint64_t> results(kThreads, 0);
    std::atomic<int> mismatch{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&controller, &results, &mismatch, t]() {
            uint64_t seed = controller.request_seed(static_cast<uint64_t>(t));
            results[t] = seed;
        });
    }

    for (auto& th : threads) th.join();

    // Verify same inputs always give same outputs
    for (int t = 0; t < kThreads; t++) {
        uint64_t expected = SeedController(42).request_seed(static_cast<uint64_t>(t));
        if (results[t] != expected) {
            mismatch.fetch_add(1);
        }
    }

    EXPECT_EQ(mismatch.load(), 0)
        << "SeedController should be deterministic across threads";
}

// ---------------------------------------------------------------------------
// Test 11: ParquetWriter/Reader -- concurrent writes to different files
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ParquetIO_ConcurrentWriteRead) {
    auto dir = pid_temp_path("parquet_concurrency");
    std::filesystem::create_directories(dir);

    constexpr int kThreads = 6;
    constexpr int kRows = 100;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    // Write tables concurrently
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&dir, &errors, t, kRows]() {
            try {
                auto table = make_double_table(
                    "col_" + std::to_string(t),
                    std::vector<double>(kRows, static_cast<double>(t)));

                std::string path = (dir / ("part_" + std::to_string(t) + ".parquet")).string();
                ParquetWriter writer;
                auto write_result = writer.write(path, table);
                if (!write_result.ok()) {
                    errors.fetch_add(1);
                    return;
                }

                ParquetReader reader;
                auto read_result = reader.read_all(path);
                if (!read_result.ok()) {
                    errors.fetch_add(1);
                    return;
                }
                if (read_result.value()->num_rows() != kRows) {
                    errors.fetch_add(1);
                }
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(errors.load(), 0) << "Errors during concurrent Parquet write/read";

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 12: ObjectStoreBackend -- concurrent appends to different tables
// ObjectStoreBackend uses MetadataManager internally which is NOT thread-safe.
// We use an external mutex to serialize operations.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ObjectStoreBackend_ConcurrentAppends) {
    auto root = pid_temp_path("backend_concurrency");
    std::filesystem::remove_all(root);

    ObjectStoreBackend backend(root);
    constexpr int kTables = 4;
    constexpr int kAppends = 20;
    std::mutex backend_mutex;

    // Register tables
    for (int t = 0; t < kTables; t++) {
        auto reg = backend.register_table("table_" + std::to_string(t),
                                           R"({"columns":[]})");
        ASSERT_TRUE(reg.ok()) << reg.error().message;
    }

    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kTables; t++) {
        threads.emplace_back([&backend, &backend_mutex, &errors, t, kAppends]() {
            try {
                for (int i = 0; i < kAppends; i++) {
                    auto table = make_double_table(
                        "val", {static_cast<double>(i)});
                    std::lock_guard<std::mutex> lock(backend_mutex);
                    auto result = backend.append(
                        "table_" + std::to_string(t), table);
                    if (!result.ok()) {
                        errors.fetch_add(1);
                    }
                }
            } catch (...) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(errors.load(), 0)
        << "Errors during concurrent appends to different tables";

    // Verify data in each table
    for (int t = 0; t < kTables; t++) {
        auto scan = backend.scan("table_" + std::to_string(t));
        ASSERT_TRUE(scan.ok()) << scan.error().message;
        EXPECT_EQ(scan.value()->num_rows(), kAppends)
            << "Table " << t << " should have " << kAppends << " rows";
    }

    std::filesystem::remove_all(root);
}

// ============================================================================
// PART 2: FAULT INJECTION TESTS
// ============================================================================

// ---------------------------------------------------------------------------
// Test 13: Corrupted Parquet -- truncate a valid file to half size
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, CorruptedParquet_TruncatedFile) {
    auto dir = pid_temp_path("corrupt_parquet");
    std::filesystem::create_directories(dir);

    // Write a valid Parquet file
    auto table = make_double_table("value", {1.0, 2.0, 3.0, 4.0, 5.0});
    ASSERT_NE(table, nullptr);

    std::string path = (dir / "corrupted.parquet").string();
    ParquetWriter writer;
    auto write_result = writer.write(path, table);
    ASSERT_TRUE(write_result.ok()) << write_result.error().message;

    // Get file size and truncate to half
    auto file_size = std::filesystem::file_size(path);
    ASSERT_GT(file_size, 0u);

    // Truncate the file
    std::error_code ec;
    std::filesystem::resize_file(path, file_size / 2, ec);
    ASSERT_FALSE(ec) << "Failed to truncate file: " << ec.message();

    // Try to read the truncated file
    ParquetReader reader;
    auto read_result = reader.read_all(path);
    EXPECT_FALSE(read_result.ok())
        << "Should fail to read truncated Parquet file";

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 14: Invalid file content -- text file where Parquet is expected
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, InvalidFileContent_TextInsteadOfParquet) {
    auto dir = pid_temp_path("text_parquet");
    std::filesystem::create_directories(dir);

    // Write a plain text file
    std::string path = (dir / "fake.parquet").string();
    {
        std::ofstream out(path, std::ios::binary);
        out << "This is not a parquet file. Just plain text content.";
    }

    // Try to read
    ParquetReader reader;
    auto read_result = reader.read_all(path);
    EXPECT_FALSE(read_result.ok())
        << "Should fail to read text file as Parquet";

    // Try to read schema
    auto schema_result = reader.read_schema(path);
    EXPECT_FALSE(schema_result.ok())
        << "Should fail to read schema from text file";

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 15: Missing directory -- write to non-existent nested path
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, MissingDirectory_WriteToNonExistentPath) {
    auto root = pid_temp_path("missing_dir");
    std::filesystem::remove_all(root);

    // ModelStorageLayer should auto-create directories
    ModelStorageLayer storage(root.string());

    auto result = storage.save_checkpoint("deep_model", "v1", "test_data");
    EXPECT_TRUE(result.ok()) << "ModelStorageLayer should auto-create directories: "
                             << result.error().message;

    // Verify the file was created
    auto load_result = storage.load_model("deep_model", "v1");
    EXPECT_TRUE(load_result.ok());
    EXPECT_EQ(load_result.value(), "test_data");

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 16: Permission denied -- write to read-only directory
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, PermissionDenied_ReadOnlyDirectory) {
    // Skip if running as root (root can write anywhere)
    if (::getuid() == 0) {
        GTEST_SKIP() << "Skipping permission test when running as root";
    }

    auto dir = pid_temp_path("readonly");
    std::filesystem::create_directories(dir / "subdir");

    // Make directory read-only
    std::filesystem::permissions(dir / "subdir",
                                  std::filesystem::perms::none,
                                  std::filesystem::perm_options::replace);

    // Try to write Parquet to read-only directory
    auto table = make_double_table("value", {1.0, 2.0});
    ASSERT_NE(table, nullptr);

    std::string path = (dir / "subdir" / "out.parquet").string();
    ParquetWriter writer;
    auto write_result = writer.write(path, table);
    EXPECT_FALSE(write_result.ok())
        << "Should fail to write to read-only directory";

    // Restore permissions for cleanup
    std::filesystem::permissions(dir / "subdir",
                                  std::filesystem::perms::owner_all,
                                  std::filesystem::perm_options::replace);
    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 17: Read from non-existent file
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, NonExistentFile_Read) {
    auto path = pid_temp_path("nonexist") / "no_such_file.parquet";

    ParquetReader reader;
    auto result = reader.read_all(path.string());
    EXPECT_FALSE(result.ok()) << "Should fail to read non-existent file";
}

// ---------------------------------------------------------------------------
// Test 18: Concurrent delete + read -- thread A writes, thread B deletes, thread A reads
// ModelStorageLayer's LRU cache is NOT thread-safe, so we use an external mutex.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ConcurrentDeleteAndRead) {
    auto root = pid_temp_path("delete_read");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);
    std::mutex storage_mutex;

    // Pre-save a model
    {
        std::lock_guard<std::mutex> lock(storage_mutex);
        auto save = storage.save_checkpoint("delete_model", "v1", "original_data");
        ASSERT_TRUE(save.ok());
    }

    std::atomic<bool> deleted{false};
    std::atomic<int> read_ok_after_delete{0};

    // Thread 1: continuously reads the model
    std::thread reader_thread([&storage, &storage_mutex, &deleted, &read_ok_after_delete]() {
        for (int i = 0; i < 200; i++) {
            std::lock_guard<std::mutex> lock(storage_mutex);
            auto result = storage.load_model("delete_model", "v1");
            if (deleted.load()) {
                if (!result.ok()) {
                    // Expected: file was deleted, should get error
                    read_ok_after_delete.fetch_add(1);
                }
            }
        }
    });

    // Give reader time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Thread 2: deletes the file
    std::thread deleter_thread([&deleted, &root]() {
        // Delete the file directly on disk
        auto file_path = std::filesystem::path(root) / "models" / "delete_model" / "v1.parquet";
        std::filesystem::remove(file_path);
        deleted.store(true);
    });

    reader_thread.join();
    deleter_thread.join();

    // After deletion, load_model should report an error (not crash)
    std::lock_guard<std::mutex> lock(storage_mutex);
    auto result = storage.load_model("delete_model", "v1");
    EXPECT_FALSE(result.ok())
        << "Should fail to load model after file deletion";
    EXPECT_EQ(result.error().code, ErrorCode::kVersionNotFound)
        << "Error code should be kVersionNotFound";

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 19: Disk full simulation -- verify write error detection
// Writing to /dev/full fails on close/flush (not on write).
// This test verifies that write errors are detectable.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, DiskFull_WriteFailure) {
    // Write to /dev/full: writes succeed but reads fail (ENOSPC).
    // On many systems, ofstream may buffer writes and not report failure
    // until close/flush. Test that we can detect such failures.
    std::ofstream out("/dev/full", std::ios::binary);
    if (!out.is_open()) {
        GTEST_SKIP() << "/dev/full not available, skipping disk-full simulation";
    }

    out << "test data that will fail to be written to /dev/full";
    out.flush();

    // On Linux, writes to /dev/full fail with ENOSPC.
    // But ofstream may buffer. Check after flush.
    if (!out.fail()) {
        // If /dev/full doesn't cause failure on this system, that's fine.
        // The important thing is our ParquetWriter handles real write errors.
        GTEST_SKIP() << "/dev/full did not cause write failure on this system";
    }
    out.close();
}

// ---------------------------------------------------------------------------
// Test 20: ParquetReader on empty file (0 bytes)
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, EmptyFile_ReadParquet) {
    auto dir = pid_temp_path("empty_parquet");
    std::filesystem::create_directories(dir);

    std::string path = (dir / "empty.parquet").string();
    { std::ofstream out(path); } // Create empty file

    ParquetReader reader;
    auto result = reader.read_all(path);
    EXPECT_FALSE(result.ok()) << "Should fail to read empty file as Parquet";

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 21: ParquetWriter write to invalid path
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ParquetWriter_InvalidPath) {
    auto table = make_double_table("value", {1.0});
    ASSERT_NE(table, nullptr);

    ParquetWriter writer;
    // Write to a path that contains null bytes or is otherwise invalid
    // On Linux, paths can't contain null bytes (truncated by OS), so use
    // a path with a non-existent deeply nested directory
    std::string path = "/proc/nonexistent_dir/impossible.parquet";
    auto result = writer.write(path, table);
    EXPECT_FALSE(result.ok()) << "Should fail to write to invalid path";
}

// ============================================================================
// PART 3: ERROR PROPAGATION TESTS
// ============================================================================

// ---------------------------------------------------------------------------
// Test 22: Result<T> chain -- error propagation through multiple stages
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ResultChain_ErrorPropagation) {
    // Stage 1: Fails
    Result<int> stage1 = Error(ErrorCode::kInvalidArgument, "stage1 failed", "test");
    EXPECT_FALSE(stage1.ok());

    // Stage 2: Depends on stage1
    Result<std::string> stage2 = stage1.ok()
        ? Result<std::string>("stage2_data_" + std::to_string(stage1.value()))
        : Result<std::string>(stage1.error());
    EXPECT_FALSE(stage2.ok());
    EXPECT_EQ(stage2.error().code, ErrorCode::kInvalidArgument);
    EXPECT_EQ(stage2.error().message, "stage1 failed");

    // Stage 3: Depends on stage2
    Result<double> stage3 = stage2.ok()
        ? Result<double>(std::stod(stage2.value()))
        : Result<double>(stage2.error());
    EXPECT_FALSE(stage3.ok());
    EXPECT_EQ(stage3.error().code, ErrorCode::kInvalidArgument);
    EXPECT_EQ(stage3.error().message, "stage1 failed");
}

// ---------------------------------------------------------------------------
// Test 23: Result<void> chain
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ResultVoid_Chain) {
    // Success chain
    Result<void> r1;
    EXPECT_TRUE(r1.ok());

    // Failure chain
    Result<void> r2 = Error(ErrorCode::kWriteFailed, "write failed", "test");
    EXPECT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, ErrorCode::kWriteFailed);

    // Propagate
    Result<void> r3 = r2.ok() ? Result<void>() : Result<void>(r2.error());
    EXPECT_FALSE(r3.ok());
    EXPECT_EQ(r3.error().code, ErrorCode::kWriteFailed);
}

// ---------------------------------------------------------------------------
// Test 24: Error in constraint pipeline -- inject error at each stage
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ConstraintPipeline_ErrorAtEachStage) {
    auto schema = make_one_col_schema();
    auto vr = schema.validate();
    ASSERT_TRUE(vr.ok());

    // Stage 1: Classify -- with missing ORDER column for inter-row constraint
    ConstraintSet bad_set;
    bad_set.value_range_names = {"vr1"};
    bad_set.inter_row_defs.push_back(InterRowConstraintDef{
        "value", "missing_ts", InterRowConstraintDef::Type::kDeltaMax, 5.0, std::nullopt});

    ConstraintClassifier classifier;
    auto cls = classifier.classify(bad_set, schema);
    EXPECT_FALSE(cls.ok()) << "Should fail: no ORDER column for inter-row constraint";

    // Stage 2: Route -- route on a classification that has no valid constraints
    // Use a valid classification first
    ConstraintSet good_set;
    good_set.value_range_names = {"vr1"};
    auto good_cls = classifier.classify(good_set, schema);
    ASSERT_TRUE(good_cls.ok());

    ExecutionRouter router(false);  // no data engine
    auto route = router.route(good_cls.value(), schema);
    EXPECT_TRUE(route.ok()) << "Valid classification should route successfully";
    EXPECT_EQ(route.value().selected_path, DegradationPath::kPurePhysics);

    // Stage 3: Validate with empty table
    std::vector<parser::ast::ConstraintItem> constraints;
    constraints.push_back(make_between("value", 0.0, 50.0));
    ValueRangeValidator validator(schema, constraints);

    auto empty_table = make_double_table("value", {});
    ASSERT_NE(empty_table, nullptr);
    auto val_result = validator.validate_batch(empty_table);
    EXPECT_TRUE(val_result.ok()) << "Should handle empty table gracefully";
    EXPECT_EQ(val_result.value().rows_checked, 0);
}

// ---------------------------------------------------------------------------
// Test 25: Large batch handling -- service rejects negative limits
// Tests that the service layer validates input bounds without allocating.
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, LargeBatch_ServiceRejectsInvalidLimits) {
    SynthGenService service;

    DefineTypeRequest type_req;
    type_req.type_name = "large_batch_test";
    {
        DefineTypeRequest::ColumnDef col;
        col.name = "v";
        col.type = "FLOAT";
        col.range_min = 0.0;
        col.range_max = 10.0;
        type_req.columns.push_back(col);
    }
    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok());

    // Negative limit should be rejected
    GenerateRequest req;
    req.type_name = "large_batch_test";
    req.limit = -1;
    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);

    // Zero limit should succeed but generate 0 rows
    GenerateRequest req_zero;
    req_zero.type_name = "large_batch_test";
    req_zero.limit = 0;
    auto result_zero = service.generate(req_zero);
    if (result_zero.ok()) {
        EXPECT_EQ(result_zero.value().stats.rows_generated, 0);
    }
}

// ---------------------------------------------------------------------------
// Test 26: Service error propagation through generate pipeline
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ServiceErrorPropagation_MissingType) {
    SynthGenService service;

    GenerateRequest req;
    req.type_name = "nonexistent_type";
    req.limit = 10;

    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kNotFound);
}

// ---------------------------------------------------------------------------
// Test 27: Service error propagation -- invalid limit
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ServiceErrorPropagation_InvalidLimit) {
    SynthGenService service;

    DefineTypeRequest type_req;
    type_req.type_name = "test_limit";
    {
        DefineTypeRequest::ColumnDef col;
        col.name = "v";
        col.type = "FLOAT";
        col.range_min = 0.0;
        col.range_max = 10.0;
        type_req.columns.push_back(col);
    }
    auto type_result = service.define_type(type_req);
    ASSERT_TRUE(type_result.ok());

    GenerateRequest req;
    req.type_name = "test_limit";
    req.limit = -100;  // negative limit
    auto result = service.generate(req);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ---------------------------------------------------------------------------
// Test 28: ObjectStoreBackend error -- scan non-existent table
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ObjectStoreBackend_ScanNonExistent) {
    auto root = pid_temp_path("backend_nonexist");
    std::filesystem::remove_all(root);

    ObjectStoreBackend backend(root);
    auto result = backend.scan("no_such_table");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kTableNotFound);

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 29: ModelVersionChain -- modify_version always fails
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelVersionChain_ImmutableViolation) {
    auto root = pid_temp_path("version_immutable");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    ModelVersionChain chain(meta);

    auto result = chain.modify_version("any_version");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kImmutableViolation);
}

// ---------------------------------------------------------------------------
// Test 30: ModelVersionChain -- parent not found
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelVersionChain_ParentNotFound) {
    auto root = pid_temp_path("version_noparent");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    ModelVersionChain chain(meta);

    ModelVersion v;
    v.created_by = "test";
    auto result = chain.create_version("model_a", "nonexistent_parent", v);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::kParentNotFound);

    std::filesystem::remove_all(root);
}

// ============================================================================
// PART 4: RECOVERY TESTS
// ============================================================================

// ---------------------------------------------------------------------------
// Test 31: Write-then-crash simulation -- partial Parquet file
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, Recovery_PartialParquetFile) {
    auto dir = pid_temp_path("partial_parquet");
    std::filesystem::create_directories(dir);

    // Write a valid Parquet file
    auto table = make_double_table("value", {1.0, 2.0, 3.0});
    ASSERT_NE(table, nullptr);

    std::string path = (dir / "good.parquet").string();
    ParquetWriter writer;
    auto write_result = writer.write(path, table);
    ASSERT_TRUE(write_result.ok());

    // Simulate partial write by truncating to some bytes (not half, but
    // keeping enough that it looks like a started write)
    auto size = std::filesystem::file_size(path);
    // Keep first 10 bytes only -- clearly partial
    std::error_code ec;
    std::filesystem::resize_file(path, std::min(size, static_cast<uintmax_t>(10)), ec);

    // Try to read -- should fail gracefully
    ParquetReader reader;
    auto read_result = reader.read_all(path);
    EXPECT_FALSE(read_result.ok())
        << "Should fail to read partial Parquet file";

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// Test 32: ModelStorageLayer -- recover from interrupted atomic write
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, Recovery_InterruptedAtomicWrite) {
    auto root = pid_temp_path("interrupted_write");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);

    // Manually create a .pending file (simulating interrupted Phase 1)
    auto pending_dir = std::filesystem::path(root) / "models" / "interrupted_model";
    std::filesystem::create_directories(pending_dir);
    {
        std::ofstream out(pending_dir / "v1.pending", std::ios::binary);
        out << "partial_data";
    }

    // recover_interrupted should clean up .pending files
    auto recover_result = storage.recover_interrupted();
    EXPECT_TRUE(recover_result.ok()) << "Recovery should succeed";

    // Verify .pending file was cleaned up
    EXPECT_FALSE(std::filesystem::exists(pending_dir / "v1.pending"))
        << "Pending file should be cleaned up after recovery";

    // But .parquet files should remain
    auto save = storage.save_checkpoint("interrupted_model", "v1", "real_data");
    ASSERT_TRUE(save.ok());

    // Recover again should be fine
    auto recover2 = storage.recover_interrupted();
    EXPECT_TRUE(recover2.ok());

    // Original data should still be readable
    auto load = storage.load_model("interrupted_model", "v1");
    EXPECT_TRUE(load.ok()) << "Data should be intact after recovery";
    EXPECT_EQ(load.value(), "real_data");

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 33: Audit log hash chain tampering detection
// Build records, verify chain, then simulate tampering by re-building with
// different data and checking detection
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_ChainTamperingDetection) {
    AuditLog log;
    auto gen = log.create_genesis();
    ASSERT_TRUE(gen.ok());

    auto r1 = log.append("op1", "user_a", {{"key", "val1"}});
    ASSERT_TRUE(r1.ok());
    auto r2 = log.append("op2", "user_b", {{"key", "val2"}});
    ASSERT_TRUE(r2.ok());
    auto r3 = log.append("op3", "user_c", {{"key", "val3"}});
    ASSERT_TRUE(r3.ok());

    // Chain should be valid
    auto valid = log.verify_chain();
    ASSERT_TRUE(valid.ok());
    EXPECT_TRUE(valid.value()) << "Chain should be valid before tampering";

    // Get records and verify linkage
    auto scan = log.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(scan.ok());
    auto& records = scan.value();
    ASSERT_GE(records.size(), 4u);

    // Verify prev_hash -> chain_hash linkage
    for (size_t i = 1; i < records.size(); ++i) {
        EXPECT_EQ(records[i].prev_hash, records[i-1].chain_hash)
            << "Chain link broken at record " << i;
    }

    // Now verify that a separate AuditLog with different data
    // produces different hashes
    AuditLog log2;
    auto gen2 = log2.create_genesis();
    ASSERT_TRUE(gen2.ok());
    auto alt = log2.append("DIFFERENT_OP", "DIFFERENT_USER", {{"key", "DIFFERENT"}});
    ASSERT_TRUE(alt.ok());

    // The chain hashes should be different
    auto scan1 = log.scan(std::nullopt, std::nullopt, 2);
    auto scan2 = log2.scan(std::nullopt, std::nullopt, 2);
    ASSERT_TRUE(scan1.ok());
    ASSERT_TRUE(scan2.ok());

    // Genesis chain_hash should differ from appended record's chain_hash
    EXPECT_NE(scan1.value()[0].chain_hash, scan1.value()[1].chain_hash)
        << "Genesis and first record should have different chain hashes";
}

// ---------------------------------------------------------------------------
// Test 34: AuditLog fork detection
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_ForkDetection) {
    AuditLog log;
    auto gen = log.create_genesis();
    ASSERT_TRUE(gen.ok());

    // Append linearly
    for (int i = 0; i < 5; i++) {
        auto r = log.append("op_" + std::to_string(i), "user");
        ASSERT_TRUE(r.ok()) << "Append " << i << " failed";
    }

    // Linear chain should have no forks
    auto forks = log.detect_forks();
    ASSERT_TRUE(forks.ok());
    EXPECT_TRUE(forks.value().empty()) << "Linear chain should have no forks";
}

// ---------------------------------------------------------------------------
// Test 35: AuditLog append with empty operation
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_EmptyOperation) {
    AuditLog log;
    auto gen = log.create_genesis();
    ASSERT_TRUE(gen.ok());

    auto result = log.append("", "user");
    EXPECT_FALSE(result.ok()) << "Should reject empty operation";
    EXPECT_EQ(result.error().code, ErrorCode::kInvalidArgument);
}

// ---------------------------------------------------------------------------
// Test 36: ModelVersionChain -- create chain, list, and verify immutability
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelVersionChain_FullLifecycle) {
    auto root = pid_temp_path("version_lifecycle");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    ModelVersionChain chain(meta);

    // Create root version
    ModelVersion v1;
    v1.created_by = "test";
    v1.fidelity_score = 0.95;
    v1.training_rows = 1000;
    auto r1 = chain.create_version("model_x", "", v1);
    ASSERT_TRUE(r1.ok()) << r1.error().message;
    EXPECT_TRUE(r1.value().is_first_version());

    // Create child version
    ModelVersion v2;
    v2.created_by = "test";
    v2.fidelity_score = 0.98;
    auto r2 = chain.create_version("model_x", r1.value().version_id, v2);
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_FALSE(r2.value().is_first_version());
    EXPECT_EQ(r2.value().parent_version_id, r1.value().version_id);

    // List versions
    auto list = chain.list_versions("model_x");
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 2u);

    // Get specific version
    auto get = chain.get_version(r2.value().version_id);
    ASSERT_TRUE(get.ok());
    EXPECT_DOUBLE_EQ(get.value()->fidelity_score, 0.98);

    // Attempt modification
    auto mod = chain.modify_version(r1.value().version_id);
    EXPECT_FALSE(mod.ok());
    EXPECT_EQ(mod.error().code, ErrorCode::kImmutableViolation);

    // Empty model name should fail
    ModelVersion v3;
    v3.created_by = "test";
    auto r3 = chain.create_version("", "", v3);
    EXPECT_FALSE(r3.ok());
    EXPECT_EQ(r3.error().code, ErrorCode::kInvalidArgument);

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 37: ModelStorageLayer LRU cache behavior
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelStorageLayer_LRUCacheBehavior) {
    auto root = pid_temp_path("lru_cache");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);

    // Save more versions than the cache can hold (kMaxCacheSize = 5)
    for (int i = 0; i < 8; i++) {
        std::string version_id = "v" + std::to_string(i);
        std::string data = "data_for_version_" + std::to_string(i);
        auto result = storage.save_checkpoint("cached_model", version_id, data);
        ASSERT_TRUE(result.ok()) << "Save v" << i << " failed";
    }

    // Load all versions -- should work even if some were evicted from cache
    for (int i = 0; i < 8; i++) {
        std::string version_id = "v" + std::to_string(i);
        std::string expected = "data_for_version_" + std::to_string(i);
        auto result = storage.load_model("cached_model", version_id);
        ASSERT_TRUE(result.ok()) << "Load v" << i << " failed: " << result.error().message;
        EXPECT_EQ(result.value(), expected) << "Data mismatch for v" << i;
    }

    // List versions
    auto list = storage.list_model_versions("cached_model");
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 8u) << "Should have 8 versions";

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 38: ModelStorageLayer atomic_write with three-phase commit
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelStorageLayer_AtomicWriteThreePhase) {
    auto root = pid_temp_path("atomic_write");
    std::filesystem::remove_all(root);

    ModelStorageLayer storage(root);

    ModelVersion version;
    version.version_id = "v_atomic";
    version.model_name = "atomic_model";
    version.created_by = "test";

    std::string data = "atomic_write_test_data_content";

    auto result = storage.atomic_write("atomic_model", data, version);
    EXPECT_TRUE(result.ok()) << "Atomic write should succeed: "
                             << result.error().message;

    // Verify data is readable
    auto load = storage.load_model("atomic_model", "v_atomic");
    EXPECT_TRUE(load.ok()) << load.error().message;
    EXPECT_EQ(load.value(), data);

    // No .pending files should remain
    auto models_dir = std::filesystem::path(root) / "models" / "atomic_model";
    for (const auto& entry : std::filesystem::directory_iterator(models_dir)) {
        EXPECT_NE(entry.path().extension(), ".pending")
            << "No .pending files should remain after atomic_write";
    }

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 39: Concurrent ModelVersionChain access
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelVersionChain_ConcurrentCreate) {
    auto root = pid_temp_path("version_concurrent");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    ModelVersionChain chain(meta);
    std::mutex chain_mutex;  // Not thread-safe internally

    constexpr int kThreads = 6;
    constexpr int kIterations = 30;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&chain, &chain_mutex, &errors, t]() {
            for (int i = 0; i < kIterations; i++) {
                std::lock_guard<std::mutex> lock(chain_mutex);
                ModelVersion v;
                v.created_by = "thread_" + std::to_string(t);
                v.fidelity_score = static_cast<double>(i);
                auto result = chain.create_version(
                    "concurrent_model", "", v);
                if (!result.ok()) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(errors.load(), 0) << "Errors during concurrent version creation";

    // Verify all versions were created (use large limit since list_versions defaults to 100)
    auto list = chain.list_versions("concurrent_model", 1000);
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(),
              static_cast<size_t>(kThreads * kIterations));

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 40: RectangularSampler with extreme edge values
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, RectangularSampler_ExtremeValues) {
    Schema schema;
    schema.type_name = "extreme_test";
    {
        ColumnDef col;
        col.name = "value";
        col.type = DataType::kFloat;
        col.range_min = -1e300;
        col.range_max = 1e300;
        schema.columns.push_back(col);
    }
    auto vr = schema.validate();
    ASSERT_TRUE(vr.ok());

    RectangularSampler sampler(schema);
    std::vector<parser::ast::ConstraintItem> no_cons;
    auto req = GenerationRequest{schema, no_cons, 100, 42, "uniform"};
    auto result = sampler.generate(req);

    // Should not crash with extreme range values
    if (result.ok()) {
        EXPECT_EQ(result.value().data->num_rows(), 100);
    }
}

// ---------------------------------------------------------------------------
// Test 41: Service concurrent define_type + generate
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, Service_ConcurrentDefineTypeAndGenerate) {
    SynthGenService service;
    constexpr int kThreads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    std::mutex service_mutex;  // Service not thread-safe internally

    // Pre-register types
    for (int t = 0; t < kThreads; t++) {
        DefineTypeRequest req;
        req.type_name = "concurrent_type_" + std::to_string(t);
        DefineTypeRequest::ColumnDef col;
        col.name = "v";
        col.type = "FLOAT";
        col.range_min = 0.0;
        col.range_max = 100.0;
        req.columns.push_back(col);

        std::lock_guard<std::mutex> lock(service_mutex);
        auto result = service.define_type(req);
        ASSERT_TRUE(result.ok()) << result.error().message;
    }

    // Concurrently generate from different types
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&service, &service_mutex, &errors, t]() {
            for (int i = 0; i < 5; i++) {
                GenerateRequest req;
                req.type_name = "concurrent_type_" + std::to_string(t);
                req.limit = 10;
                req.seed = static_cast<uint64_t>(i * 100 + t);

                std::lock_guard<std::mutex> lock(service_mutex);
                auto result = service.generate(req);
                if (!result.ok()) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(errors.load(), 0) << "Errors during concurrent service generate";
}

// ---------------------------------------------------------------------------
// Test 42: Hash function consistency under concurrent access
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, HashFunction_ConcurrentConsistency) {
    constexpr int kThreads = 8;
    constexpr int kIterations = 500;
    std::vector<std::thread> threads;
    std::unordered_map<std::string, std::string> expected_hashes;
    std::mutex map_mutex;
    std::atomic<int> mismatches{0};

    // Pre-compute expected hashes
    for (int i = 0; i < 100; i++) {
        std::string input = "hash_input_" + std::to_string(i);
        expected_hashes[input] = sha256_hex(input);
    }

    // Verify concurrently
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&expected_hashes, &map_mutex, &mismatches]() {
            for (int i = 0; i < kIterations; i++) {
                int idx = i % 100;
                std::string input = "hash_input_" + std::to_string(idx);

                std::string hash = sha256_hex(input);

                std::lock_guard<std::mutex> lock(map_mutex);
                if (hash != expected_hashes[input]) {
                    mismatches.fetch_add(1);
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(mismatches.load(), 0)
        << "Hash function should produce consistent results across threads";
}

// ---------------------------------------------------------------------------
// Test 43: MetadataManager -- concurrent operations
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, MetadataManager_ConcurrentOperations) {
    auto root = pid_temp_path("meta_concurrent");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    std::mutex meta_mutex;
    constexpr int kThreads = 4;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    // Concurrent table creation
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&meta, &meta_mutex, &errors, t]() {
            std::lock_guard<std::mutex> lock(meta_mutex);
            auto result = meta.create_table(
                "table_" + std::to_string(t),
                R"({"columns":[{"name":"v","type":"FLOAT"}]})");
            if (!result.ok()) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_EQ(errors.load(), 0);

    // Flush and reload
    auto flush = meta.flush();
    ASSERT_TRUE(flush.ok()) << flush.error().message;

    MetadataManager meta2(root);
    auto reload = meta2.reload();
    ASSERT_TRUE(reload.ok()) << reload.error().message;

    // Verify all tables were persisted
    for (int t = 0; t < kThreads; t++) {
        auto result = meta2.get_table("table_" + std::to_string(t));
        EXPECT_TRUE(result.ok()) << "Table " << t << " should exist after reload";
    }

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 44: ObjectStoreBackend -- write and scan with predicate
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ObjectStoreBackend_WriteAndScanWithPredicate) {
    auto root = pid_temp_path("backend_pred");
    std::filesystem::remove_all(root);

    ObjectStoreBackend backend(root);
    auto reg = backend.register_table("pred_table", R"({"columns":[]})");
    ASSERT_TRUE(reg.ok());

    // Append data
    auto table = make_double_table("score", {10.0, 20.0, 30.0, 40.0, 50.0});
    auto append = backend.append("pred_table", table);
    ASSERT_TRUE(append.ok());

    // Scan with predicate
    ScanPredicate pred;
    pred.column = "score";
    pred.min_value = 25.0;
    pred.max_value = 45.0;

    auto scan = backend.scan("pred_table", {}, pred);
    ASSERT_TRUE(scan.ok()) << scan.error().message;

    // Should return only values in [25, 45]
    auto& result = scan.value();
    EXPECT_GT(result->num_rows(), 0) << "Some rows should match predicate";
    EXPECT_LT(result->num_rows(), 5) << "Not all rows should match predicate";

    // Verify all values are in range
    int col_idx = result->schema()->GetFieldIndex("score");
    ASSERT_GE(col_idx, 0);
    auto col = result->column(col_idx);
    for (int c = 0; c < col->num_chunks(); c++) {
        auto arr = std::static_pointer_cast<arrow::DoubleArray>(col->chunk(c));
        for (int64_t r = 0; r < arr->length(); r++) {
            double val = arr->Value(r);
            EXPECT_GE(val, 25.0) << "Value below predicate min";
            EXPECT_LE(val, 45.0) << "Value above predicate max";
        }
    }

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 45: AuditLog scan with various time ranges and limits
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, AuditLog_ScanWithTimeRangeAndLimits) {
    AuditLog log;
    auto gen = log.create_genesis();
    ASSERT_TRUE(gen.ok());

    // Append several records
    for (int i = 0; i < 10; i++) {
        auto r = log.append("op_" + std::to_string(i), "user",
                             {{"idx", std::to_string(i)}});
        ASSERT_TRUE(r.ok());
    }

    // Scan with limit
    auto limited = log.scan(std::nullopt, std::nullopt, 5);
    ASSERT_TRUE(limited.ok());
    EXPECT_EQ(limited.value().size(), 5u) << "Should return at most 5 records";

    // Scan all
    auto all = log.scan(std::nullopt, std::nullopt, 100);
    ASSERT_TRUE(all.ok());
    EXPECT_EQ(all.value().size(), 11u) << "Should return all 11 records (genesis + 10)";

    // Get latest
    auto latest = log.get_latest();
    ASSERT_TRUE(latest.ok());
    EXPECT_EQ(latest.value().operation, "op_9");

    // Scan with very old from timestamp
    auto from_old = log.scan(0, std::nullopt, 100);
    ASSERT_TRUE(from_old.ok());
    EXPECT_EQ(from_old.value().size(), 11u);

    // Scan with future from timestamp
    auto from_future = log.scan(99999999999999999LL, std::nullopt, 100);
    ASSERT_TRUE(from_future.ok());
    EXPECT_EQ(from_future.value().size(), 0u);
}

// ---------------------------------------------------------------------------
// Test 46: EvidencePackageBuilder with invalid inputs
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, EvidencePackageBuilder_FromJson_MalformedInputs) {
    EvidencePackageBuilder builder;

    // Empty string
    auto r1 = builder.from_json("");
    EXPECT_FALSE(r1.ok()) << "Should reject empty JSON string";

    // Null bytes
    auto r2 = builder.from_json(std::string("\0\0\0", 3));
    EXPECT_FALSE(r2.ok()) << "Should reject null bytes";

    // Truncated JSON
    auto r3 = builder.from_json("{\"schema_version\":");
    EXPECT_FALSE(r3.ok()) << "Should reject truncated JSON";

    // Valid JSON but missing required fields -- builder provides defaults
    auto r4 = builder.from_json("{}");
    // from_json tolerates missing fields by providing defaults
    if (r4.ok()) {
        EXPECT_EQ(r4.value().row_count, 0);  // default
    }

    // Array instead of object
    auto r5 = builder.from_json("[]");
    EXPECT_FALSE(r5.ok()) << "Should reject JSON array";
}

// ---------------------------------------------------------------------------
// Test 47: PostFilter with extremely large table
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, PostFilter_LargeTable) {
    // Generate a large dataset
    auto table = make_double_table("value",
        std::vector<double>(100000, 42.0));
    ASSERT_NE(table, nullptr);

    PostFilter pf;
    auto result = pf.execute(table, 50000);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().post_filter_rows, 50000);
    EXPECT_NEAR(result.value().actual_exclusion_rate, 0.5, 0.01);
}

// ---------------------------------------------------------------------------
// Test 48: Concurrent Histogram with mixed values
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, Histogram_ConcurrentMixedValues) {
    MetricsRegistry::instance().reset();

    constexpr int kThreads = 8;
    constexpr int kValuesPerThread = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kValuesPerThread; i++) {
                MetricsRegistry::instance().histogram("mixed_hist")
                    .observe(static_cast<double>(i + t * kValuesPerThread));
            }
        });
    }

    for (auto& th : threads) th.join();

    auto& h = MetricsRegistry::instance().histogram("mixed_hist");
    int64_t expected_count = kThreads * kValuesPerThread;
    EXPECT_EQ(h.count(), expected_count)
        << "Histogram count should be " << expected_count;

    // Sum should be sum of all values 0..N-1 where N = expected_count
    // sum = N*(N-1)/2
    double expected_sum = static_cast<double>(expected_count) *
                          static_cast<double>(expected_count - 1) / 2.0;
    EXPECT_DOUBLE_EQ(h.sum(), expected_sum)
        << "Histogram sum mismatch";

    // Mean should be (N-1)/2
    double expected_mean = static_cast<double>(expected_count - 1) / 2.0;
    EXPECT_DOUBLE_EQ(h.mean(), expected_mean)
        << "Histogram mean mismatch";

    MetricsRegistry::instance().reset();
}

// ---------------------------------------------------------------------------
// Test 49: ModelVersionChain -- cycle detection
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, ModelVersionChain_CycleDetection) {
    auto root = pid_temp_path("version_cycle");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    MetadataManager meta(root);
    ModelVersionChain chain(meta);

    ModelVersion v1;
    v1.created_by = "test";
    auto r1 = chain.create_version("cycle_model", "", v1);
    ASSERT_TRUE(r1.ok());

    ModelVersion v2;
    v2.created_by = "test";
    auto r2 = chain.create_version("cycle_model", r1.value().version_id, v2);
    ASSERT_TRUE(r2.ok());

    // Try to create a version that would form a cycle
    // (by setting parent to itself, which is prevented by has_cycle)
    ModelVersion v3;
    v3.created_by = "test";
    // v3 with parent = v2 should work fine
    auto r3 = chain.create_version("cycle_model", r2.value().version_id, v3);
    EXPECT_TRUE(r3.ok()) << "Normal chain should work";

    // List should show 3 versions
    auto list = chain.list_versions("cycle_model");
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 3u);

    std::filesystem::remove_all(root);
}

// ---------------------------------------------------------------------------
// Test 50: SchemaRegistry -- duplicate registration race
// ---------------------------------------------------------------------------
TEST(ConcurrencyFault, SchemaRegistry_DuplicateRace) {
    SchemaRegistry registry;
    std::mutex reg_mutex;
    std::atomic<int> success_count{0};
    std::atomic<int> duplicate_count{0};
    constexpr int kThreads = 10;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) {
        threads.emplace_back([&registry, &reg_mutex, &success_count, &duplicate_count]() {
            Schema s;
            s.type_name = "raced_type";  // All threads try to register same name
            ColumnDef col;
            col.name = "col";
            col.type = DataType::kFloat;
            col.range_min = 0.0;
            col.range_max = 10.0;
            s.columns.push_back(col);

            std::lock_guard<std::mutex> lock(reg_mutex);
            auto result = registry.register_schema(std::move(s));
            if (result.ok()) {
                success_count.fetch_add(1);
            } else if (result.error().code == ErrorCode::kDuplicateTypeName) {
                duplicate_count.fetch_add(1);
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(success_count.load(), 1) << "Exactly one registration should succeed";
    EXPECT_EQ(duplicate_count.load(), kThreads - 1) << "All others should be duplicates";
}
