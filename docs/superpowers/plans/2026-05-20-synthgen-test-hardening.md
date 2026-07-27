# SynthGen Core 测试增强与冒烟测试计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 大幅增强测试覆盖，添加冒烟测试验证全链路端到端可工作，确保"实施完成"时系统真正能运行。

**Architecture:** 三层策略：(1) 端到端冒烟测试（E2E Smoke）验证 Parser → Schema → Engine → Evidence → Storage 全链路；(2) Service 级集成测试绕过 HTTP 层直接测试 SynthGenService 的完整流程；(3) 缺失的单元测试覆盖关键错误路径和边界条件。新增测试文件放 `tests/e2e/` 和 `tests/integration/` 目录。

**Tech Stack:** C++17, Google Test, Google Mock, Apache Arrow, cpp-httplib

---

## 当前状态

- 772 个测试全部通过 (99% pass rate, 实际 761 pass / 11 flaky)
- **0 个 E2E 测试**（`tests/e2e/` 目录为空）
- 无 Service 级别集成测试（现有 api_test.cpp 依赖 HTTP 线程，不够稳定）
- 缺少关键错误路径测试：Service 层错误处理、跨版本数据流、Storage 与 Engine 的联合操作
- 无冒烟测试脚本验证"构建后系统能否运行"

## 文件结构

| 文件 | 职责 |
|------|------|
| `tests/e2e/smoke_test.cpp` | **新建** - 5 个冒烟测试覆盖全链路 |
| `tests/e2e/service_integration_test.cpp` | **新建** - Service 级集成测试（12 个场景） |
| `tests/e2e/pipeline_stress_test.cpp` | **新建** - 压力测试（大数据量、多约束、多版本） |
| `tests/integration/storage_engine_test.cpp` | **新建** - Storage + Engine 联合集成测试 |
| `tests/integration/constraint_pipeline_test.cpp` | **新建** - 约束引擎完整管道测试 |
| `tests/CMakeLists.txt` | **修改** - 添加新测试目标 |

---

### Task 1: E2E 冒烟测试 — 核心全链路

**Files:**
- Create: `tests/e2e/smoke_test.cpp`
- Modify: `tests/CMakeLists.txt`

**目标:** 5 个冒烟测试验证系统能从 SynthLang DSL → 数据生成 → 存储 → 审计 的完整路径。

- [ ] **Step 1: 创建冒烟测试文件**

```cpp
// tests/e2e/smoke_test.cpp
// E2E 冒烟测试: 验证 SynthGen Core 核心链路端到端可工作
// 不依赖 HTTP 服务器，直接调用内部组件

#include <gtest/gtest.h>

#include "parser/lexer.h"
#include "parser/parser.h"
#include "schema/schema.h"
#include "schema/schema_builder.h"
#include "schema/schema_registry.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/evidence/tail_report.h"
#include "engine/evidence/evidence_package_builder.h"
#include "engine/evidence/evidence_package_json.h"
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/postfilter/post_filter.h"
#include "storage/object_store_backend.h"
#include "storage/audit/audit_log.h"
#include "scaffold/trace.h"
#include "scaffold/metrics.h"
#include "common/hash.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <filesystem>
#include <string>

using namespace synthgen;
namespace fs = std::filesystem;

namespace {
// 共享临时目录
std::string make_smoke_dir() {
    auto dir = fs::temp_directory_path() / "synthgen_smoke_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir.string();
}
} // namespace

// ============================================================================
// Smoke 1: SynthLang 解析 → Schema 构建 → 验证
// ============================================================================
TEST(SmokeTest, SynthLangToSchema) {
    const char* dsl = R"(
        DEFINE TYPE sensor_log {
            timestamp: DATETIME NOT NULL ORDER,
            temperature: FLOAT [-50.0, 80.0],
            pressure: FLOAT [900.0, 1100.0],
            status: ENUM('normal', 'warning', 'fault')
        };
    )";

    // Step 1: Lex
    parser::Lexer lexer(dsl);
    auto tokens = lexer.tokenize();
    ASSERT_TRUE(tokens.ok()) << "Lexer failed: " << tokens.error().message;
    EXPECT_GT(tokens.value().size(), 5u);

    // Step 2: Parse
    parser::Parser parser;
    auto parse_result = parser.parse(dsl);
    ASSERT_TRUE(parse_result.ok()) << "Parser failed: " << parse_result.error().message;
    EXPECT_EQ(parse_result.value().program.statements.size(), 1u);

    // Step 3: Build schema
    auto* stmt = std::get_if<parser::ast::DefineTypeStmt>(
        &parse_result.value().program.statements[0]);
    ASSERT_NE(stmt, nullptr);
    auto schema = schema::SchemaBuilder::build(*stmt);
    ASSERT_TRUE(schema.ok()) << "Schema build failed: " << schema.error().message;

    // Step 4: Validate schema
    EXPECT_EQ(schema.value().type_name, "sensor_log");
    EXPECT_EQ(schema.value().columns.size(), 4u);
    auto vr = schema.value().validate();
    EXPECT_TRUE(vr.ok()) << vr.error().message;

    // Step 5: Verify ORDER column
    auto order_cols = schema.value().order_columns();
    EXPECT_EQ(order_cols.size(), 1u);
    EXPECT_EQ(order_cols[0], "timestamp");
}

// ============================================================================
// Smoke 2: Schema → 数据生成 → 约束验证
// ============================================================================
TEST(SmokeTest, SchemaGenerateAndValidate) {
    // Build schema
    schema::Schema s;
    s.type_name = "pressure_sensor";
    {
        schema::ColumnDef temp;
        temp.name = "temperature";
        temp.type = DataType::kFloat;
        temp.range_min = -50.0;
        temp.range_max = 80.0;
        s.columns.push_back(temp);
    }
    {
        schema::ColumnDef press;
        press.name = "pressure";
        press.type = DataType::kFloat;
        press.range_min = 900.0;
        press.range_max = 1100.0;
        s.columns.push_back(press);
    }

    // Generate data
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0},
        {"pressure", parser::ast::ConstraintOperator::kBetween, 950.0, 1050.0}
    };
    engine::physics::GenerationRequest gen_req{s, constraints, 500, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok()) << gen_result.error().message;
    EXPECT_EQ(gen_result.value().data->num_rows(), 500);
    EXPECT_EQ(gen_result.value().stats.rows_generated, 500);

    // Validate constraints
    engine::constraint::ValueRangeValidator validator(s, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok()) << val_result.error().message;

    // With physics-first sampling within constraint range, pass rate should be high
    EXPECT_GT(val_result.value().pass_rate, 0.8)
        << "Pass rate too low: " << val_result.value().pass_rate;

    // Verify data columns exist
    EXPECT_EQ(gen_result.value().data->num_columns(), 2);
}

// ============================================================================
// Smoke 3: 数据生成 → EvidencePackage → JSON 序列化/反序列化
// ============================================================================
TEST(SmokeTest, GenerateToEvidencePackage) {
    schema::Schema s;
    s.type_name = "temp_sensor";
    {
        schema::ColumnDef col;
        col.name = "temperature";
        col.type = DataType::kFloat;
        col.range_min = -50.0;
        col.range_max = 80.0;
        s.columns.push_back(col);
    }

    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0}
    };

    // Generate
    engine::physics::GenerationRequest gen_req{s, constraints, 100, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok());

    // Validate
    engine::constraint::ValueRangeValidator validator(s, constraints);
    auto val_result = validator.validate_batch(gen_result.value().data);
    ASSERT_TRUE(val_result.ok());

    // Build tail report
    engine::evidence::TailReportBuilder tail_builder;
    auto tail = tail_builder.build(gen_result.value(), val_result.value(),
                                    gen_req, constraints);
    ASSERT_TRUE(tail.ok());

    // Build evidence package
    engine::evidence::ProvenanceV1 prov;
    prov.data_source = "synthetic";
    prov.generator_identity = "physics_sampler";
    prov.constraints = {"safe_temp"};
    prov.generation_params = {42, "uniform", 100, 1000};
    engine::evidence::EvidencePackageBuilder evp_builder;
    auto evp = evp_builder.build(gen_result.value(), val_result.value(),
                                  tail.value(), prov, s);
    ASSERT_TRUE(evp.ok()) << evp.error().message;

    // Serialize to JSON
    auto json_str = evp_builder.to_json(evp.value());
    ASSERT_TRUE(json_str.ok()) << json_str.error().message;
    EXPECT_GT(json_str.value().size(), 50u);

    // Verify key fields present
    EXPECT_NE(json_str.value().find("physics_guaranteed"), std::string::npos);
    EXPECT_NE(json_str.value().find("not_applicable"), std::string::npos);
    EXPECT_NE(json_str.value().find("physical_first"), std::string::npos);

    // Deserialize and verify round-trip
    auto parsed = engine::evidence::from_json(json_str.value());
    ASSERT_TRUE(parsed.ok()) << parsed.error().message;
    EXPECT_EQ(parsed.value().schema_version, "v1");
    EXPECT_EQ(parsed.value().row_count, 100);
    EXPECT_EQ(parsed.value().data_grade, "physics_guaranteed");
}

// ============================================================================
// Smoke 4: 数据生成 → Storage 存储 → 扫描读回
// ============================================================================
TEST(SmokeTest, GenerateStoreAndReadback) {
    auto dir = make_smoke_dir();
    storage::ObjectStoreBackend backend(dir);

    // Define schema
    schema::Schema s;
    s.type_name = "storage_sensor";
    {
        schema::ColumnDef temp;
        temp.name = "temperature";
        temp.type = DataType::kFloat;
        temp.range_min = -50.0;
        temp.range_max = 80.0;
        s.columns.push_back(temp);
    }

    // Register table
    auto reg = backend.register_table("storage_sensor", "{}");
    ASSERT_TRUE(reg.ok()) << reg.error().message;

    // Generate data
    engine::physics::GenerationRequest gen_req{s, {}, 200, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);
    auto gen_result = sampler.generate(gen_req);
    ASSERT_TRUE(gen_result.ok());

    // Store
    auto append_result = backend.append("storage_sensor", gen_result.value().data);
    ASSERT_TRUE(append_result.ok()) << append_result.error().message;

    // Read back
    auto scan_result = backend.scan("storage_sensor");
    ASSERT_TRUE(scan_result.ok()) << scan_result.error().message;
    EXPECT_EQ(scan_result.value()->num_rows(), 200);
    EXPECT_EQ(scan_result.value()->num_columns(), 1);

    // Verify data integrity: temperature column exists
    auto temp_col = std::static_pointer_cast<arrow::DoubleArray>(
        scan_result.value()->column(0)->chunk(0));
    for (int64_t i = 0; i < temp_col->length(); ++i) {
        EXPECT_GE(temp_col->Value(i), -50.0);
        EXPECT_LE(temp_col->Value(i), 80.0);
    }
}

// ============================================================================
// Smoke 5: 审计日志链完整性
// ============================================================================
TEST(SmokeTest, AuditLogChainIntegrity) {
    storage::audit::AuditLog audit;
    audit.create_genesis();

    // Simulate a full generation pipeline with audit steps
    audit.append("define_type", "service", {{"type", "sensor_log"}});
    audit.append("define_constraint", "service", {{"constraint", "safe_temp"}});
    audit.append("classify", "constraint_classifier", {{"value_range", "1"}});
    audit.append("route", "execution_router", {{"path", "pure_physics"}});
    audit.append("generate", "physics_sampler", {{"rows", "500"}});
    audit.append("validate", "value_range_validator", {{"pass_rate", "0.95"}});
    audit.append("evidence_build", "evidence_builder", {{"version", "v1"}});
    audit.append("store", "storage_backend", {{"table", "sensor_log"}});

    EXPECT_EQ(audit.record_count(), 9); // genesis + 8 steps

    // Verify chain integrity
    auto verify = audit.verify_chain();
    ASSERT_TRUE(verify.ok()) << verify.error().message;
    EXPECT_TRUE(verify.value());

    // Daily verification
    auto report = audit.daily_verification();
    ASSERT_TRUE(report.ok());
    EXPECT_TRUE(report.value().is_valid);
    EXPECT_EQ(report.value().total_records, 9);
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加 e2e 测试目标**

在 `tests/CMakeLists.txt` 末尾追加：

```cmake
# E2E smoke tests
add_synthgen_test(smoke_test e2e/smoke_test.cpp)
```

- [ ] **Step 3: 构建并运行冒烟测试**

Run: `cd $(PROJECT_DIR)/build && cmake .. && make -j$(nproc) smoke_test && ./tests/smoke_test --gtest_filter="SmokeTest.*"`

Expected: 5/5 tests PASSED

- [ ] **Step 4: Commit**

```bash
git add tests/e2e/smoke_test.cpp tests/CMakeLists.txt
git commit -m "test: add E2E smoke tests (5 scenarios) for full pipeline validation"
```

---

### Task 2: Service 级集成测试 — 绕过 HTTP 直接测试业务逻辑

**Files:**
- Create: `tests/e2e/service_integration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**目标:** 12 个场景直接调用 SynthGenService，覆盖所有 5 个公共方法 + 错误路径 + 组合场景。

- [ ] **Step 1: 创建 Service 集成测试**

```cpp
// tests/e2e/service_integration_test.cpp
// 直接调用 SynthGenService 测试完整业务流程（绕过 HTTP 层）

#include <gtest/gtest.h>
#include "api/service.h"
#include "api/request.h"
#include "api/response.h"

using namespace synthgen::api;

class ServiceIntegrationTest : public ::testing::Test {
protected:
    SynthGenService service;

    Result<SchemaRef> define_sensor(const std::string& name = "sensor") {
        DefineTypeRequest req;
        req.type_name = name;
        req.columns = {
            {"temperature", "FLOAT", -50.0, 80.0, {}, false, false},
            {"pressure", "FLOAT", 900.0, 1100.0, {}, false, false}
        };
        return service.define_type(req);
    }

    Result<ConstraintRef> define_constraint(
            const std::string& cname, const std::string& tname,
            const std::vector<RangeCheck>& checks) {
        DefineConstraintRequest req;
        req.constraint_name = cname;
        req.type_name = tname;
        req.checks = checks;
        return service.define_constraint(req);
    }
};

// ----- define_type 正常路径 -----

TEST_F(ServiceIntegrationTest, DefineTypeBasic) {
    auto r = define_sensor();
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().type_name, "sensor");
    EXPECT_EQ(r.value().column_count, 2);
}

TEST_F(ServiceIntegrationTest, DefineTypeMultipleTypes) {
    auto r1 = define_sensor("sensor_a");
    auto r2 = define_sensor("sensor_b");
    auto r3 = define_sensor("sensor_c");
    ASSERT_TRUE(r1.ok());
    ASSERT_TRUE(r2.ok());
    ASSERT_TRUE(r3.ok());
    EXPECT_EQ(r1.value().type_name, "sensor_a");
    EXPECT_EQ(r3.value().type_name, "sensor_c");
}

TEST_F(ServiceIntegrationTest, DefineTypeEnumColumn) {
    DefineTypeRequest req;
    req.type_name = "status_type";
    req.columns = {
        {"status", "ENUM", std::nullopt, std::nullopt,
         {"normal", "warning", "fault"}, false, false}
    };
    auto r = service.define_type(req);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().column_count, 1);
}

// ----- define_type 错误路径 -----

TEST_F(ServiceIntegrationTest, DefineTypeEmptyName) {
    DefineTypeRequest req;
    req.type_name = "";
    req.columns = {{"x", "FLOAT", 0.0, 1.0, {}, false, false}};
    auto r = service.define_type(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
}

TEST_F(ServiceIntegrationTest, DefineTypeNoColumns) {
    DefineTypeRequest req;
    req.type_name = "empty_type";
    auto r = service.define_type(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
}

// ----- load_data 正常/错误路径 -----

TEST_F(ServiceIntegrationTest, LoadDataTypeExists) {
    define_sensor("ld_sensor");
    LoadDataRequest req;
    req.type_name = "ld_sensor";
    req.path = "/data/test.parquet";
    req.mode = "strict";
    auto r = service.load_data(req);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().status, "success");
}

TEST_F(ServiceIntegrationTest, LoadDataTypeNotFound) {
    LoadDataRequest req;
    req.type_name = "nonexistent";
    auto r = service.load_data(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

// ----- define_constraint 正常/错误路径 -----

TEST_F(ServiceIntegrationTest, DefineConstraintBasic) {
    define_sensor("dc_sensor");
    auto r = define_constraint("safe_temp", "dc_sensor",
        {{"temperature", -10.0, 45.0}});
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().constraint_name, "safe_temp");
    EXPECT_EQ(r.value().check_count, 1);
}

TEST_F(ServiceIntegrationTest, DefineConstraintMultipleChecks) {
    define_sensor("mc_sensor");
    auto r = define_constraint("multi_check", "mc_sensor",
        {{"temperature", -10.0, 45.0}, {"pressure", 950.0, 1050.0}});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().check_count, 2);
}

TEST_F(ServiceIntegrationTest, DefineConstraintTypeNotFound) {
    auto r = define_constraint("bad", "nonexistent",
        {{"x", 0.0, 1.0}});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

// ----- generate 完整链路 -----

TEST_F(ServiceIntegrationTest, GenerateFullPipeline) {
    define_sensor("gen_sensor");
    define_constraint("safe_gen", "gen_sensor",
        {{"temperature", -10.0, 45.0}});

    GenerateRequest req;
    req.type_name = "gen_sensor";
    req.constraints = {"safe_gen"};
    req.limit = 200;
    req.seed = 42;
    req.distribution = "uniform";
    auto r = service.generate(req);

    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().stats.rows_generated, 200);
    EXPECT_GT(r.value().evidence_json.size(), 50u);
    EXPECT_NE(r.value().evidence_json.find("physics_guaranteed"), std::string::npos);
}

TEST_F(ServiceIntegrationTest, GenerateWithMultipleConstraints) {
    define_sensor("multi_gen");
    define_constraint("c1", "multi_gen",
        {{"temperature", -10.0, 45.0}});
    define_constraint("c2", "multi_gen",
        {{"pressure", 950.0, 1050.0}});

    GenerateRequest req;
    req.type_name = "multi_gen";
    req.constraints = {"c1", "c2"};
    req.limit = 100;
    req.seed = 42;
    auto r = service.generate(req);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().stats.rows_generated, 100);
}

TEST_F(ServiceIntegrationTest, GenerateTypeNotFound) {
    GenerateRequest req;
    req.type_name = "nonexistent";
    req.limit = 10;
    auto r = service.generate(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kNotFound);
}

TEST_F(ServiceIntegrationTest, GenerateInvalidLimit) {
    define_sensor("inv_sensor");
    GenerateRequest req;
    req.type_name = "inv_sensor";
    req.limit = 0;
    auto r = service.generate(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
}

TEST_F(ServiceIntegrationTest, GenerateNegativeLimit) {
    define_sensor("neg_sensor");
    GenerateRequest req;
    req.type_name = "neg_sensor";
    req.limit = -5;
    auto r = service.generate(req);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::kInvalidArgument);
}

// ----- explain 正常路径 -----

TEST_F(ServiceIntegrationTest, ExplainBasic) {
    define_sensor("ex_sensor");
    ExplainRequest req;
    req.type_name = "ex_sensor";
    req.constraints = {"safe_temp"};
    auto r = service.explain(req);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value().execution_mode, "row_by_row");
}

// ----- health 正常路径 -----

TEST_F(ServiceIntegrationTest, HealthCheck) {
    auto r = service.health();
    EXPECT_EQ(r.components.count("parser"), 1u);
    EXPECT_EQ(r.components.count("storage"), 1u);
    EXPECT_EQ(r.components.count("physics_engine"), 1u);
    EXPECT_EQ(r.components.at("parser"), "ok");
}

// ----- 端到端完整流程: define → constraint → generate → verify evidence -----

TEST_F(ServiceIntegrationTest, FullWorkflowDefineConstraintGenerate) {
    // 1. Define type with multiple columns including ENUM
    DefineTypeRequest dt_req;
    dt_req.type_name = "industrial_sensor";
    dt_req.columns = {
        {"temperature", "FLOAT", -50.0, 200.0, {}, false, false},
        {"pressure", "FLOAT", 800.0, 1200.0, {}, false, false},
        {"vibration", "FLOAT", 0.0, 50.0, {}, false, false}
    };
    auto dt = service.define_type(dt_req);
    ASSERT_TRUE(dt.ok()) << dt.error().message;

    // 2. Define multiple constraints
    auto dc1 = define_constraint("safe_temp", "industrial_sensor",
        {{"temperature", -10.0, 80.0}});
    ASSERT_TRUE(dc1.ok());

    auto dc2 = define_constraint("safe_pressure", "industrial_sensor",
        {{"pressure", 900.0, 1100.0}});
    ASSERT_TRUE(dc2.ok());

    // 3. Generate with all constraints
    GenerateRequest gen_req;
    gen_req.type_name = "industrial_sensor";
    gen_req.constraints = {"safe_temp", "safe_pressure"};
    gen_req.limit = 1000;
    gen_req.seed = 12345;
    auto gen = service.generate(gen_req);
    ASSERT_TRUE(gen.ok()) << gen.error().message;
    EXPECT_EQ(gen.value().stats.rows_generated, 1000);
    EXPECT_GT(gen.value().stats.elapsed_ms, 0);

    // 4. Verify evidence package contains expected fields
    EXPECT_NE(gen.value().evidence_json.find("industrial_sensor"), std::string::npos);
    EXPECT_NE(gen.value().evidence_json.find("physics_guaranteed"), std::string::npos);
    EXPECT_NE(gen.value().evidence_json.find("1000"), std::string::npos);
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加目标**

追加到 `tests/CMakeLists.txt`:

```cmake
add_synthgen_test(service_integration_test e2e/service_integration_test.cpp)
```

- [ ] **Step 3: 构建并运行**

Run: `cd $(PROJECT_DIR)/build && make -j$(nproc) service_integration_test && ./tests/service_integration_test`

Expected: ALL TESTS PASSED

- [ ] **Step 4: Commit**

```bash
git add tests/e2e/service_integration_test.cpp tests/CMakeLists.txt
git commit -m "test: add Service-level integration tests (16 scenarios) bypassing HTTP"
```

---

### Task 3: Storage + Engine 联合集成测试

**Files:**
- Create: `tests/integration/storage_engine_test.cpp`
- Modify: `tests/CMakeLists.txt`

**目标:** 6 个场景验证 Storage 和 Engine 的联合操作：生成数据存入 Storage、读取后验证、跨版本操作。

- [ ] **Step 1: 创建 Storage + Engine 联合测试**

```cpp
// tests/integration/storage_engine_test.cpp
// Storage + Engine 联合集成测试

#include <gtest/gtest.h>
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "storage/object_store_backend.h"
#include "schema/schema.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <filesystem>

using namespace synthgen;
namespace fs = std::filesystem;

namespace {
schema::Schema make_test_schema() {
    schema::Schema s;
    s.type_name = "test_data";
    {
        schema::ColumnDef col;
        col.name = "value";
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        s.columns.push_back(col);
    }
    return s;
}

std::string make_temp_dir() {
    auto dir = fs::temp_directory_path() / "synthgen_storage_engine_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir.string();
}
} // namespace

// 生成 → 存储 → 读回 → 验证数据完整性
TEST(StorageEngineTest, GenerateStoreReadback) {
    auto dir = make_temp_dir();
    storage::ObjectStoreBackend backend(dir);
    auto schema = make_test_schema();

    backend.register_table("test_data", "{}");

    engine::physics::GenerationRequest req{schema, {}, 500, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());

    backend.append("test_data", gen.value().data);

    auto scan = backend.scan("test_data");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 500);

    // Verify all values in range
    auto col = std::static_pointer_cast<arrow::DoubleArray>(
        scan.value()->column(0)->chunk(0));
    for (int64_t i = 0; i < col->length(); ++i) {
        EXPECT_GE(col->Value(i), 0.0);
        EXPECT_LE(col->Value(i), 100.0);
    }
}

// 生成 → 约束验证 → 只有通过约束的行才存入 Storage
TEST(StorageEngineTest, GenerateValidateFilterStore) {
    auto dir = make_temp_dir();
    storage::ObjectStoreBackend backend(dir);
    auto schema = make_test_schema();

    backend.register_table("filtered_data", "{}");

    std::vector<parser::ast::ConstraintItem> constraints = {
        {"value", parser::ast::ConstraintOperator::kBetween, 20.0, 80.0}
    };

    engine::physics::GenerationRequest req{schema, constraints, 1000, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());

    // Validate
    engine::constraint::ValueRangeValidator validator(schema, constraints);
    auto val = validator.validate_batch(gen.value().data);
    ASSERT_TRUE(val.ok());

    // Store original data
    backend.append("filtered_data", gen.value().data);

    auto scan = backend.scan("filtered_data");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 1000);

    // Verify stored data respects constraints (physics-first should be within range)
    auto col = std::static_pointer_cast<arrow::DoubleArray>(
        scan.value()->column(0)->chunk(0));
    int in_range = 0;
    for (int64_t i = 0; i < col->length(); ++i) {
        if (col->Value(i) >= 20.0 && col->Value(i) <= 80.0) in_range++;
    }
    EXPECT_GT(in_range, 800); // Most should be in range with physics-first
}

// 多次生成追加到同一张表
TEST(StorageEngineTest, MultipleGenerationsAppendToSameTable) {
    auto dir = make_temp_dir();
    storage::ObjectStoreBackend backend(dir);
    auto schema = make_test_schema();

    backend.register_table("multi_gen", "{}");

    engine::physics::RectangularSampler sampler(schema);
    for (int i = 0; i < 5; ++i) {
        engine::physics::GenerationRequest req{schema, {}, 100,
            static_cast<uint64_t>(42 + i), "uniform", 1000};
        auto gen = sampler.generate(req);
        ASSERT_TRUE(gen.ok());
        auto append = backend.append("multi_gen", gen.value().data);
        ASSERT_TRUE(append.ok()) << append.error().message;
    }

    auto scan = backend.scan("multi_gen");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 500);

    auto versions = backend.list_versions("multi_gen");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 5u);
}

// 列投影扫描返回正确数据
TEST(StorageEngineTest, ColumnProjectionAfterGeneration) {
    auto dir = make_temp_dir();
    storage::ObjectStoreBackend backend(dir);

    schema::Schema s;
    s.type_name = "multi_col";
    s.columns = {
        {"temp", DataType::kFloat, -50.0, 80.0, {}, false, false, {}},
        {"pressure", DataType::kFloat, 900.0, 1100.0, {}, false, false, {}}
    };

    backend.register_table("multi_col", "{}");

    engine::physics::GenerationRequest req{s, {}, 100, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());
    backend.append("multi_col", gen.value().data);

    // Scan only pressure
    auto scan = backend.scan("multi_col", {"pressure"});
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    EXPECT_EQ(scan.value()->num_columns(), 1);
    EXPECT_EQ(scan.value()->num_rows(), 100);
    EXPECT_EQ(scan.value()->schema()->field(0)->name(), "pressure");
}

// 带谓词扫描过滤生成数据
TEST(StorageEngineTest, ScanWithPredicateOnGeneratedData) {
    auto dir = make_temp_dir();
    storage::ObjectStoreBackend backend(dir);
    auto schema = make_test_schema();

    backend.register_table("pred_data", "{}");

    engine::physics::GenerationRequest req{schema, {}, 1000, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(schema);
    auto gen = sampler.generate(req);
    ASSERT_TRUE(gen.ok());
    backend.append("pred_data", gen.value().data);

    storage::ScanPredicate pred;
    pred.column = "value";
    pred.min_value = 40.0;
    pred.max_value = 60.0;

    auto scan = backend.scan("pred_data", {}, pred);
    ASSERT_TRUE(scan.ok()) << scan.error().message;
    for (int64_t i = 0; i < scan.value()->num_rows(); ++i) {
        auto col = std::static_pointer_cast<arrow::DoubleArray>(
            scan.value()->column(0)->chunk(0));
        EXPECT_GE(col->Value(i), 40.0);
        EXPECT_LE(col->Value(i), 60.0);
    }
}

// 重启模拟: 存储数据后重新打开 backend 验证持久化
TEST(StorageEngineTest, RestartPersistenceWithGeneratedData) {
    auto dir = make_temp_dir();
    auto schema = make_test_schema();

    std::string table_id = "persistent_gen";

    {
        storage::ObjectStoreBackend backend(dir);
        backend.register_table(table_id, "{}");
        engine::physics::GenerationRequest req{schema, {}, 200, 42, "uniform", 1000};
        engine::physics::RectangularSampler sampler(schema);
        auto gen = sampler.generate(req);
        ASSERT_TRUE(gen.ok());
        backend.append(table_id, gen.value().data);
    }

    // Simulate restart
    {
        storage::ObjectStoreBackend backend2(dir);
        auto has = backend2.has_table(table_id);
        ASSERT_TRUE(has.ok());
        EXPECT_TRUE(has.value());

        auto scan = backend2.scan(table_id);
        ASSERT_TRUE(scan.ok()) << scan.error().message;
        EXPECT_EQ(scan.value()->num_rows(), 200);
    }
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加目标**

追加到 `tests/CMakeLists.txt`:

```cmake
# Storage + Engine integration tests
add_synthgen_test(storage_engine_test integration/storage_engine_test.cpp)
```

- [ ] **Step 3: 构建并运行**

Run: `cd $(PROJECT_DIR)/build && make -j$(nproc) storage_engine_test && ./tests/storage_engine_test`

Expected: ALL TESTS PASSED

- [ ] **Step 4: Commit**

```bash
git add tests/integration/storage_engine_test.cpp tests/CMakeLists.txt
git commit -m "test: add Storage+Engine integration tests (6 scenarios)"
```

---

### Task 4: 约束引擎管道测试

**Files:**
- Create: `tests/integration/constraint_pipeline_test.cpp`
- Modify: `tests/CMakeLists.txt`

**目标:** 6 个场景验证完整约束管道：Classify → Route → Generate → Validate → PostFilter → Evidence v2。

- [ ] **Step 1: 创建约束管道测试**

```cpp
// tests/integration/constraint_pipeline_test.cpp
// 约束引擎完整管道集成测试
// 验证: Classify → Route → Generate → InterRow → Aggregate → PostFilter → Evidence v2

#include <gtest/gtest.h>
#include "engine/router/constraint_classifier.h"
#include "engine/router/execution_router.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/physics/rectangular_sampler.h"
#include "engine/postfilter/post_filter.h"
#include "engine/evidence/evidence_package_v2_builder.h"
#include "storage/audit/audit_log.h"
#include "schema/schema.h"
#include "scaffold/trace.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>

using namespace synthgen;
using namespace synthgen::engine::router;
using namespace synthgen::engine::constraint;
using namespace synthgen::engine::physics;
using namespace synthgen::engine::postfilter;
using namespace synthgen::engine::evidence;
using namespace synthgen::storage::audit;
using namespace synthgen::schema;

namespace {
Schema make_full_schema() {
    Schema s;
    s.type_name = "pipeline_test";
    {
        ColumnDef ts; ts.name = "timestamp"; ts.type = DataType::kDatetime;
        ts.is_order = true; ts.not_null = true; s.columns.push_back(ts);
    }
    {
        ColumnDef temp; temp.name = "temperature"; temp.type = DataType::kFloat;
        temp.range_min = -50.0; temp.range_max = 80.0; s.columns.push_back(temp);
    }
    {
        ColumnDef vib; vib.name = "vibration"; vib.type = DataType::kFloat;
        vib.range_min = 0.0; vib.range_max = 10.0; s.columns.push_back(vib);
    }
    return s;
}

std::shared_ptr<arrow::Table> make_arrow_table(
        const std::vector<int64_t>& ts,
        const std::vector<double>& temp,
        const std::vector<double>& vib = {}) {
    arrow::Int64Builder ts_b;
    arrow::DoubleBuilder temp_b;
    arrow::DoubleBuilder vib_b;
    for (auto t : ts) ts_b.Append(t);
    for (auto t : temp) temp_b.Append(t);
    if (vib.empty()) {
        for (size_t i = 0; i < temp.size(); ++i) vib_b.Append(0.0);
    } else {
        for (auto v : vib) vib_b.Append(v);
    }
    auto ts_arr = *ts_b.Finish();
    auto temp_arr = *temp_b.Finish();
    auto vib_arr = *vib_b.Finish();
    return arrow::Table::Make(
        arrow::schema({
            arrow::field("timestamp", arrow::int64()),
            arrow::field("temperature", arrow::float64()),
            arrow::field("vibration", arrow::float64())
        }), {ts_arr, temp_arr, vib_arr});
}

constexpr int64_t kHour = 3600000000LL;
} // namespace

// ---- 场景 1: 只有 value-range → kPurePhysics ----
TEST(ConstraintPipelineTest, ValueRangeOnly_PurePhysicsPath) {
    auto schema = make_full_schema();
    SpanGuard::active_spans().clear();

    // Classify
    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());
    EXPECT_EQ(cls.value().execution_mode, ExecutionMode::kRowByRow);

    // Route
    ExecutionRouter router(false);
    auto route = router.route(cls.value(), schema);
    ASSERT_TRUE(route.ok());
    EXPECT_EQ(route.value().selected_path, DegradationPath::kPurePhysics);

    // Generate
    std::vector<parser::ast::ConstraintItem> constraints = {
        {"temperature", parser::ast::ConstraintOperator::kBetween, -10.0, 45.0}
    };
    GenerationRequest gen_req{schema, constraints, 500, 42, "uniform", 1000};
    RectangularSampler sampler(schema);
    auto gen = sampler.generate(gen_req);
    ASSERT_TRUE(gen.ok());

    // Validate
    ValueRangeValidator validator(schema, constraints);
    auto val = validator.validate_batch(gen.value().data);
    ASSERT_TRUE(val.ok());
    EXPECT_GT(val.value().pass_rate, 0.8);

    // PostFilter
    PostFilter pf;
    auto pf_result = pf.execute(gen.value().data, 500);
    ASSERT_TRUE(pf_result.ok());

    // Evidence V2
    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(500, pf_result.value().actual_exclusion_rate,
        PostFilter::data_grade_for_band(pf_result.value().rate_band),
        route.value(), cls.value(), pf_result.value(), schema);
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ep.value().schema_version, "v2");
    EXPECT_EQ(ep.value().constraint_type_breakdown.value_range_count, 1);
}

// ---- 场景 2: value-range + inter-row → kPostFilter ----
TEST(ConstraintPipelineTest, ValueRangeAndInterRow_PostFilterPath) {
    auto schema = make_full_schema();
    SpanGuard::active_spans().clear();

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 5.0;

    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    cs.inter_row_defs.push_back(ird);
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());
    EXPECT_EQ(cls.value().execution_mode, ExecutionMode::kStatefulBatch);

    ExecutionRouter router(true);
    auto route = router.route(cls.value(), schema);
    ASSERT_TRUE(route.ok());
    EXPECT_EQ(route.value().selected_path, DegradationPath::kPostFilter);

    // Execute inter-row
    InterRowEngine ir_engine(schema, {ird});
    auto table = make_arrow_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {20.0, 22.0, 25.0, 30.0, 32.0, 35.0, 40.0, 42.0, 45.0, 48.0});
    auto ir_result = ir_engine.execute_batch(table, {});
    ASSERT_TRUE(ir_result.ok());

    PostFilter pf;
    auto pf_result = pf.execute(ir_result.value().filtered_batch,
                                  ir_result.value().rows_passed);
    ASSERT_TRUE(pf_result.ok());

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(
        ir_result.value().rows_passed, pf_result.value().actual_exclusion_rate,
        PostFilter::data_grade_for_band(pf_result.value().rate_band),
        route.value(), cls.value(), pf_result.value(), schema);
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ep.value().constraint_type_breakdown.inter_row_count, 1);
}

// ---- 场景 3: 全约束类型 → kFullFunction ----
TEST(ConstraintPipelineTest, AllConstraintTypes_FullFunctionPath) {
    auto schema = make_full_schema();
    SpanGuard::active_spans().clear();

    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 20.0;
    cs.inter_row_defs.push_back(ird);

    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 50.0;
    acd.window_interval_us = kHour;
    cs.aggregate_defs.push_back(acd);

    ConstraintClassifier classifier;
    auto cls = classifier.classify(cs, schema);
    ASSERT_TRUE(cls.ok());
    EXPECT_EQ(cls.value().execution_mode, ExecutionMode::kTwoPhase);

    ExecutionRouter router(true);
    auto route = router.route(cls.value(), schema);
    ASSERT_TRUE(route.ok());
    EXPECT_EQ(route.value().selected_path, DegradationPath::kFullFunction);

    // Execute aggregate
    AggregateEngine agg_engine(schema, {acd});
    auto table = make_arrow_table(
        {0, 100, 200, kHour, kHour + 100},
        {20.0, 25.0, 30.0, 22.0, 28.0});
    auto agg = agg_engine.execute(table, {});
    ASSERT_TRUE(agg.ok());

    PostFilter pf;
    auto pf_result = pf.execute(agg.value().phase_one_output, 5);
    ASSERT_TRUE(pf_result.ok());

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(5, 0.0, "physics_guaranteed",
        route.value(), cls.value(), pf_result.value(), schema);
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ep.value().constraint_type_breakdown.value_range_count, 1);
    EXPECT_EQ(ep.value().constraint_type_breakdown.inter_row_count, 1);
    EXPECT_EQ(ep.value().constraint_type_breakdown.aggregate_count, 1);
}

// ---- 场景 4: InterRow 约束严格过滤 ----
TEST(ConstraintPipelineTest, InterRowStrictFiltering) {
    auto schema = make_full_schema();

    InterRowConstraintDef ird;
    ird.column_name = "temperature";
    ird.type = InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 2.0; // Very strict

    InterRowEngine engine(schema, {ird});

    // Create data where many consecutive rows have large jumps
    auto table = make_arrow_table(
        {0, 100, 200, 300, 400, 500, 600, 700, 800, 900},
        {10.0, 20.0, 25.0, 50.0, 51.0, 52.0, 80.0, 81.0, 82.0, 83.0});
    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok());

    // 10→20 (diff=10, fail), 20→25 (diff=5, fail), 25→50 (fail),
    // 50→51 (pass), 51→52 (pass), 52→80 (fail), 80→81 (pass), 81→82 (pass), 82→83 (pass)
    // First row always passes (no previous), so at least 1
    EXPECT_LT(result.value().rows_passed, 10);
    EXPECT_GT(result.value().rows_passed, 0);
}

// ---- 场景 5: AggregateEngine 窗口计算 ----
TEST(ConstraintPipelineTest, AggregateWindowComputation) {
    auto schema = make_full_schema();

    AggregateConstraintDef acd;
    acd.constraint_name = "avg_temp";
    acd.column_name = "temperature";
    acd.function = AggregateFunction::kAvg;
    acd.max_val = 40.0;
    acd.window_interval_us = kHour;

    AggregateEngine engine(schema, {acd});

    // Data spanning 2 hours
    auto table = make_arrow_table(
        {0, 100, 200, kHour, kHour + 100, kHour + 200, 2*kHour},
        {20.0, 25.0, 30.0, 20.0, 25.0, 30.0, 60.0});
    auto result = engine.execute(table, {});
    ASSERT_TRUE(result.ok());

    // Phase one output should exist
    EXPECT_NE(result.value().phase_one_output, nullptr);
}

// ---- 场景 6: 审计日志贯穿约束管道 ----
TEST(ConstraintPipelineTest, AuditTrailThroughPipeline) {
    AuditLog audit;
    audit.create_genesis();

    auto schema = make_full_schema();

    ConstraintClassifier classifier;
    ConstraintSet cs;
    cs.value_range_names = {"temp_range"};
    auto cls = classifier.classify(cs, schema);
    audit.append("classify", "classifier");

    ExecutionRouter router(false);
    auto route = router.route(cls.value(), schema);
    audit.append("route", "router");

    auto table = make_arrow_table({0, 100, 200}, {25.0, 30.0, 35.0});
    audit.append("generate", "sampler", {{"rows", "3"}});

    PostFilter pf;
    auto pf_result = pf.execute(table, 3);
    audit.append("post_filter", "post_filter");

    EvidencePackageV2Builder ep_builder;
    auto ep = ep_builder.build(3, 0.0, "physics_guaranteed",
        route.value(), cls.value(), pf_result.value(), schema);
    audit.append("evidence_build", "evidence_builder");

    EXPECT_EQ(audit.record_count(), 6);
    auto verify = audit.verify_chain();
    ASSERT_TRUE(verify.ok());
    EXPECT_TRUE(verify.value());
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加目标**

```cmake
# Constraint pipeline integration tests
add_synthgen_test(constraint_pipeline_test integration/constraint_pipeline_test.cpp)
```

- [ ] **Step 3: 构建并运行**

Run: `cd $(PROJECT_DIR)/build && make -j$(nproc) constraint_pipeline_test && ./tests/constraint_pipeline_test`

Expected: ALL TESTS PASSED

- [ ] **Step 4: Commit**

```bash
git add tests/integration/constraint_pipeline_test.cpp tests/CMakeLists.txt
git commit -m "test: add constraint pipeline integration tests (6 scenarios)"
```

---

### Task 5: 压力测试 — 大数据量、多约束、多版本

**Files:**
- Create: `tests/e2e/pipeline_stress_test.cpp`
- Modify: `tests/CMakeLists.txt`

**目标:** 4 个压力场景验证系统在边界条件下的稳定性。

- [ ] **Step 1: 创建压力测试**

```cpp
// tests/e2e/pipeline_stress_test.cpp
// 压力测试: 大数据量、多约束、多版本、宽表

#include <gtest/gtest.h>
#include "engine/physics/rectangular_sampler.h"
#include "engine/constraint/value_range_validator.h"
#include "engine/constraint/inter_row_engine.h"
#include "engine/constraint/aggregate_engine.h"
#include "engine/postfilter/post_filter.h"
#include "storage/object_store_backend.h"
#include "schema/schema.h"

#include <arrow/table.h>
#include <arrow/array.h>
#include <arrow/builder.h>
#include <arrow/type.h>
#include <filesystem>
#include <chrono>

using namespace synthgen;
namespace fs = std::filesystem;

// ---- 压力 1: 50 列宽表生成 10000 行 ----
TEST(PipelineStressTest, WideTableGeneration) {
    schema::Schema s;
    s.type_name = "wide_table";
    for (int i = 0; i < 50; ++i) {
        schema::ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = static_cast<double>(i);
        col.range_max = static_cast<double>(i + 1);
        s.columns.push_back(col);
    }

    engine::physics::GenerationRequest req{s, {}, 10000, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);

    auto start = std::chrono::steady_clock::now();
    auto result = sampler.generate(req);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_EQ(result.value().data->num_rows(), 10000);
    EXPECT_EQ(result.value().data->num_columns(), 50);
    // Should complete in reasonable time (< 30 seconds)
    EXPECT_LT(elapsed, 30000) << "Wide table generation took " << elapsed << "ms";
}

// ---- 压力 2: 大量约束的验证 ----
TEST(PipelineStressTest, ManyConstraintsValidation) {
    schema::Schema s;
    s.type_name = "many_constraints";
    for (int i = 0; i < 20; ++i) {
        schema::ColumnDef col;
        col.name = "col_" + std::to_string(i);
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        s.columns.push_back(col);
    }

    // 20 constraints, one per column
    std::vector<parser::ast::ConstraintItem> constraints;
    for (int i = 0; i < 20; ++i) {
        constraints.push_back({
            "col_" + std::to_string(i),
            parser::ast::ConstraintOperator::kBetween,
            20.0, 80.0
        });
    }

    engine::physics::GenerationRequest gen_req{s, constraints, 1000, 42, "uniform", 1000};
    engine::physics::RectangularSampler sampler(s);
    auto gen = sampler.generate(gen_req);
    ASSERT_TRUE(gen.ok());

    engine::constraint::ValueRangeValidator validator(s, constraints);
    auto val = validator.validate_batch(gen.value().data);
    ASSERT_TRUE(val.ok());
    EXPECT_EQ(val.value().rows_checked, 1000);
}

// ---- 压力 3: 100 次追加到 Storage ----
TEST(PipelineStressTest, HundredAppendsWithGeneration) {
    auto dir = fs::temp_directory_path() / "synthgen_stress_storage";
    fs::remove_all(dir);
    fs::create_directories(dir);

    storage::ObjectStoreBackend backend(dir.string());
    backend.register_table("stress_table", "{}");

    schema::Schema s;
    s.type_name = "stress";
    {
        schema::ColumnDef col;
        col.name = "value";
        col.type = DataType::kFloat;
        col.range_min = 0.0;
        col.range_max = 100.0;
        s.columns.push_back(col);
    }

    engine::physics::RectangularSampler sampler(s);

    for (int i = 0; i < 100; ++i) {
        engine::physics::GenerationRequest req{s, {}, 10,
            static_cast<uint64_t>(42 + i), "uniform", 1000};
        auto gen = sampler.generate(req);
        ASSERT_TRUE(gen.ok()) << "Generation failed at batch " << i;
        auto append = backend.append("stress_table", gen.value().data);
        ASSERT_TRUE(append.ok()) << "Append failed at batch " << i
                                  << ": " << append.error().message;
    }

    auto scan = backend.scan("stress_table");
    ASSERT_TRUE(scan.ok());
    EXPECT_EQ(scan.value()->num_rows(), 1000);

    auto versions = backend.list_versions("stress_table");
    ASSERT_TRUE(versions.ok());
    EXPECT_EQ(versions.value().size(), 100u);
}

// ---- 压力 4: InterRow 引擎处理大状态序列 ----
TEST(PipelineStressTest, InterRowLargeStateSequence) {
    schema::Schema s;
    s.type_name = "state_seq";
    {
        schema::ColumnDef ts; ts.name = "ts"; ts.type = DataType::kDatetime;
        ts.is_order = true; s.columns.push_back(ts);
    }
    {
        schema::ColumnDef val; val.name = "value"; val.type = DataType::kFloat;
        val.range_min = 0.0; val.range_max = 100.0; s.columns.push_back(val);
    }

    engine::constraint::InterRowConstraintDef ird;
    ird.column_name = "value";
    ird.type = engine::constraint::InterRowConstraintDef::Type::kDeltaMax;
    ird.delta_max = 10.0;

    engine::constraint::InterRowEngine engine(s, {ird});

    // Build table with 5000 rows
    arrow::Int64Builder ts_b;
    arrow::DoubleBuilder val_b;
    for (int64_t i = 0; i < 5000; ++i) {
        ts_b.Append(i * 100);
        // Gradual increase that should mostly pass the delta constraint
        val_b.Append(static_cast<double>(i) * 0.001);
    }
    auto ts_arr = *ts_b.Finish();
    auto val_arr = *val_b.Finish();
    auto table = arrow::Table::Make(
        arrow::schema({arrow::field("ts", arrow::int64()),
                        arrow::field("value", arrow::float64())}),
        {ts_arr, val_arr});

    auto result = engine.execute_batch(table, {});
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().rows_passed, 5000); // All should pass (gradual change)
}
```

- [ ] **Step 2: 在 CMakeLists.txt 中添加目标**

```cmake
# Pipeline stress tests
add_synthgen_test(pipeline_stress_test e2e/pipeline_stress_test.cpp)
```

- [ ] **Step 3: 构建并运行**

Run: `cd $(PROJECT_DIR)/build && make -j$(nproc) pipeline_stress_test && ./tests/pipeline_stress_test`

Expected: ALL TESTS PASSED

- [ ] **Step 4: Commit**

```bash
git add tests/e2e/pipeline_stress_test.cpp tests/CMakeLists.txt
git commit -m "test: add pipeline stress tests (4 scenarios) for large-scale validation"
```

---

### Task 6: 确认所有 772 个原有测试 + 新测试全部通过

**Files:** 无新文件

**目标:** 完整回归测试，确保新增测试没有破坏任何现有功能。

- [ ] **Step 1: 完整构建并运行全部测试**

Run: `cd $(PROJECT_DIR)/build && cmake .. && make -j$(nproc) && ctest -j$(nproc) --output-on-failure`

Expected: ~800+ tests, ALL PASSED (原有 772 + 新增 ~33)

- [ ] **Step 2: 检查测试总数和通过率**

Run: `cd $(PROJECT_DIR)/build && ctest -N | tail -1`

Expected: 测试总数 ≥ 805

- [ ] **Step 3: 运行最终验证 commit**

```bash
git add -A
git commit -m "test: test hardening complete — smoke + integration + stress tests added"
```

---

## Self-Review

**1. Spec coverage check:**
- SynthLang DSL 解析 → Smoke 1 ✓
- Schema 构建/验证 → Smoke 1, Service tests ✓
- Physics 引擎生成 → Smoke 2, Stress 1-2 ✓
- 约束验证 → Smoke 2, Constraint pipeline 1-6 ✓
- Evidence Package → Smoke 3 ✓
- Storage 存储/读回 → Smoke 4, Storage-Engine 1-6, Stress 3 ✓
- Audit 日志 → Smoke 5, Constraint pipeline 6 ✓
- Service 层 → Service integration 1-16 ✓
- v2 约束管道 → Constraint pipeline 1-6 ✓
- 端到端全链路 → Smoke 1-5, Service FullWorkflow ✓

**2. Placeholder scan:** 无 TODO/TBD/placeholder — 所有代码完整。

**3. Type consistency:**
- `GenerationRequest` 参数: `{schema, constraints, limit, seed, distribution, batch_size}` — 与 `rectangular_sampler.h` 一致
- `ConstraintSet` 使用 `value_range_names`, `inter_row_defs`, `aggregate_defs` — 与 `constraint_classifier.h` 一致
- `PostFilter::execute(data, target_rows)` — 与 `post_filter.h` 一致
- `EvidencePackageV2Builder::build(...)` 参数顺序 — 与 `evidence_package_v2_builder.h` 一致

**潜在编译问题:** `schema::ColumnDef` 初始化列表 `{{...}}` 可能需要与实际 struct 成员顺序匹配。在实施时需验证 `ColumnDef` 的确切字段顺序。
