# v1 Unit G Plan — SDK + REST API

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-g-plan.md
> 编译日期：2026-05-14

## 摘要

Unit G 实现 Python SDK 和 REST API，估算 1 周，依赖 Unit F（EvidencePackage Builder）。包含 5 个 Task、14 个步骤：C++ 核心服务（SynthGenService 封装全部生成逻辑）、REST API（基于 cpp-httplib 的 HTTP 服务器，7 个端点）、Python SDK（pybind11 绑定 + Python 封装层）、端到端测试（完整用户流程 + 性能测试）、脚手架集成（Health/Metrics/Trace 端点）。SynthLang 为内部编译目标，用户通过 SDK/HTTP 交互。

## 关键要点

- SynthGenService 是 C++ 核心门面类，封装 Parser、SchemaRegistry、RectangularSampler、ValueRangeValidator、TailReportBuilder、EvidencePackageBuilder
- REST API 7 个端点：POST /v1/types、POST /v1/types/{name}/data、POST /v1/constraints、POST /v1/explain、POST /v1/generate、GET /v1/metrics、GET /v1/health
- Python SDK 使用 pybind11 绑定 + Python 层封装，提供 Pythonic API
- 性能指标：1000 行 < 1s、10000 行 < 5s、100 并发 < 10s、API 延迟 < 100ms
- 并发请求使用线程池，限制并发数

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `SynthGenService` | `src/api/service.h/.cpp` | C++ 核心服务，封装全部生成逻辑 |
| 请求/响应结构 | `src/api/request.h`, `src/api/response.h` | 所有请求和响应的 JSON 序列化/反序列化 |
| HTTP 服务器 | `src/api/server.h/.cpp` | 基于 cpp-httplib 的 HTTP 服务器 |
| 请求处理器 | `src/api/handlers.h/.cpp` | 每个端点的请求处理逻辑 |
| pybind11 绑定 | `src/sdk/bindings.cpp` | C++ -> Python 绑定 |
| Python 封装 | `python/synthgen/__init__.py` | Pythonic API + 类型提示 + 文档字符串 |

### REST API 端点

| 方法 | 路径 | 功能 |
|------|------|------|
| POST | /v1/types | 定义类型（DEFINE TYPE） |
| POST | /v1/types/{name}/data | 导入数据（LOAD DATA） |
| POST | /v1/constraints | 定义约束（DEFINE CONSTRAINT） |
| POST | /v1/explain | 查询执行计划 |
| POST | /v1/generate | 生成数据 |
| GET | /v1/metrics | Prometheus 格式指标 |
| GET | /v1/health | 健康检查 |

### SynthGenService 接口

```cpp
class SynthGenService {
public:
    explicit SynthGenService(StorageBackend& storage);
    Result<SchemaRef> define_type(const DefineTypeRequest& req);
    Result<ImportResult> load_data(const LoadDataRequest& req);
    Result<ConstraintRef> define_constraint(const DefineConstraintRequest& req);
    Result<ExplainResult> explain(const ExplainRequest& req);
    Result<GenerationResult> generate(const GenerateRequest& req);
};
```

### 测试策略

- 服务单元测试 10+ 用例
- API 集成测试 15+ 用例（错误测试 >= 30%）
- Python SDK 测试 12+ 用例
- 端到端测试 8+ 用例
- 性能测试 4+ 用例（1000/10000 行、100 并发、API 延迟）
- 并发测试：10 个同时 generate

## 提取的实体

- [[synthgen-service]] — C++ 核心服务门面类，封装全部生成逻辑（新实体）
- [[rest-api-v1]] — v1 REST API，7 个端点的 HTTP 接口（新实体）
- [[python-sdk]] — Python SDK，pybind11 绑定 + Pythonic 封装（新实体）
