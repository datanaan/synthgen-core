SynthGen Core v1 Unit G 实施计划：SDK + REST API
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit G 设计规范 v1.0
估算：1 周
依赖：Unit F (EvidencePackage Builder)

---

## 概述

Unit G 实现 Python SDK 和 REST API。SynthLang 为内部编译目标，用户通过 SDK/HTTP 交互。

---

## Task 1：C++ 核心服务

**目标**：实现 C++ 端的生成服务

### Step 1.1：服务框架

**做什么**：实现 SynthGenService 类，封装所有生成逻辑

**产出**：`src/api/service.h`, `src/api/service.cpp`

```cpp
class SynthGenService {
public:
    explicit SynthGenService(StorageBackend& storage);

    Result<SchemaRef> define_type(const DefineTypeRequest& req);
    Result<ImportResult> load_data(const LoadDataRequest& req);
    Result<ConstraintRef> define_constraint(const DefineConstraintRequest& req);
    Result<ExplainResult> explain(const ExplainRequest& req);
    Result<GenerationResult> generate(const GenerateRequest& req);

private:
    Parser parser_;
    SchemaRegistry registry_;
    RectangularSampler sampler_;
    ValueRangeValidator validator_;
    TailReportBuilder tail_builder_;
    EvidencePackageBuilder evidence_builder_;
};
```

**验收**：
- [ ] 服务框架可编译
- [ ] 所有方法有正确签名

### Step 1.2：请求/响应结构

**做什么**：定义所有请求和响应结构

**产出**：`src/api/request.h`, `src/api/response.h`

**验收**：
- [ ] 所有请求结构定义完整
- [ ] 所有响应结构定义完整
- [ ] JSON 序列化/反序列化正确

### Step 1.3：服务测试

**做什么**：编写服务单元测试

**产出**：`tests/unit/service_test.cpp`

**测试用例**（至少 10 个）：
- define_type 成功
- load_data 成功
- define_constraint 成功
- explain 成功
- generate 成功
- **错误测试**：不存在的 type → 错误
- **错误测试**：不存在的约束 → 错误
- **错误测试**：非法参数 → 错误
- **边界测试**：空请求
- **边界测试**：超大请求

**验收**：10+ 测试用例全通过

---

## Task 2：REST API

**目标**：实现 HTTP 端点

### Step 2.1：HTTP 服务器

**做什么**：使用 cpp-httplib 实现 HTTP 服务器

**产出**：`src/api/server.h`, `src/api/server.cpp`

**端点**：
- POST /v1/types
- POST /v1/types/{name}/data
- POST /v1/constraints
- POST /v1/explain
- POST /v1/generate
- GET /v1/metrics
- GET /v1/health

**验收**：
- [ ] 服务器可启动
- [ ] 所有端点可访问
- [ ] 正确路由

### Step 2.2：请求处理

**做什么**：实现每个端点的请求处理

**产出**：`src/api/handlers.h`, `src/api/handlers.cpp`

**关键逻辑**：
- 解析 JSON 请求
- 调用 SynthGenService
- 序列化 JSON 响应
- 错误处理

**验收**：
- [ ] 正确解析请求
- [ ] 正确调用服务
- [ ] 正确返回响应
- [ ] 错误时返回正确状态码

### Step 2.3：API 测试

**做什么**：编写 API 集成测试

**产出**：`tests/integration/api_test.cpp`

**测试用例**（至少 15 个）：
- POST /v1/types 成功
- POST /v1/types/{name}/data 成功
- POST /v1/constraints 成功
- POST /v1/explain 成功
- POST /v1/generate 成功
- GET /v1/metrics 成功
- GET /v1/health 成功
- **错误测试**：非法 JSON → 400
- **错误测试**：不存在的 type → 404
- **错误测试**：不存在的约束 → 404
- **错误测试**：v1 不支持语法 → 400
- **错误测试**：请求体过大 → 413
- **错误测试**：非法参数 → 400
- **边界测试**：空请求体 → 400
- **边界测试**：并发请求（10 个同时 generate）

**验收**：15+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 3：Python SDK

**目标**：实现 Python 绑定

### Step 3.1：pybind11 绑定

**做什么**：实现 C++ → Python 绑定

**产出**：`src/sdk/bindings.cpp`

**关键逻辑**：
- 绑定 SynthGenClient
- 绑定 Column、RangeCheck
- 绑定结果类型

**验收**：
- [ ] Python 可导入 synthgen 模块
- [ ] 可创建 SynthGenClient
- [ ] 可调用所有方法

### Step 3.2：Python 封装

**做什么**：编写 Python 层封装（更友好的 API）

**产出**：`python/synthgen/__init__.py`

**验收**：
- [ ] Pythonic API
- [ ] 类型提示
- [ ] 文档字符串

### Step 3.3：SDK 测试

**做什么**：编写 Python SDK 测试

**产出**：`python/tests/test_sdk.py`

**测试用例**（至少 12 个）：
- 创建客户端
- define_type
- load_data
- define_constraint
- explain
- generate
- **错误测试**：连接失败 → ConnectionError
- **错误测试**：超时 → TimeoutError
- **错误测试**：非法参数 → ValueError
- **边界测试**：limit = 0
- **边界测试**：limit = 1
- **边界测试**：空约束

**验收**：12+ 测试用例全通过

---

## Task 4：端到端测试

**目标**：验证完整用户流程

### Step 4.1：端到端测试

**做什么**：编写端到端测试

**产出**：`tests/e2e/full_pipeline_test.cpp`

**测试用例**（至少 8 个）：
- 完整流程：define → load → constraint → explain → generate
- 生成数据为有效 Parquet
- EvidencePackage 完整
- tail_report 包含偏差声明
- 固定 seed → 相同输出
- **错误测试**：完整流程中某步失败 → 正确错误
- **边界测试**：最小生成（1 行）
- **边界测试**：大生成（100000 行）

**验收**：8+ 测试用例全通过

### Step 4.2：性能测试

**做什么**：验证性能指标

**测试用例**：
- 1000 行生成 < 1s
- 10000 行生成 < 5s
- 100 个并发请求 < 10s
- API 延迟 < 100ms（空请求）

**验收**：性能指标达标

---

## Task 5：脚手架集成

**目标**：添加 Trace/Metrics/Health

### Step 5.1：Health 端点

**做什么**：实现 /v1/health

```json
{
    "status": "healthy",
    "version": "v1.0.0",
    "components": {
        "parser": "ok",
        "storage": "ok",
        "physics_engine": "ok"
    }
}
```

**验收**：健康检查返回正确

### Step 5.2：Metrics 端点

**做什么**：实现 /v1/metrics（Prometheus 格式）

```
generation_total 100
generation_rows 100000
validation_total 100
validation_passed 100
evidence_package_total 100
```

**验收**：Prometheus 可采集

### Step 5.3：Trace

**做什么**：为 API 调用添加 span

- 每个 HTTP 请求 → span
- 错误请求 → span.status = "error"

**验收**：每次请求产生 span

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: C++ 服务 | 3 | 0.3w | ⬜ |
| Task 2: REST API | 3 | 0.3w | ⬜ |
| Task 3: Python SDK | 3 | 0.2w | ⬜ |
| Task 4: E2E 测试 | 2 | 0.15w | ⬜ |
| Task 5: 脚手架 | 3 | 0.05w | ⬜ |
| **合计** | **14** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| pybind11 编译复杂 | 使用 CMake + pybind11 的 find_package |
| HTTP 服务器性能 | cpp-httplib 足够 v1，v2+ 可换 |
| Python/C++ 版本兼容 | 支持 Python 3.8+ |
| 并发请求处理 | 使用线程池，限制并发数 |
