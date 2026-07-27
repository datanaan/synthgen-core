SynthGen Core v1 Unit G 设计规范：SDK + REST API
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit F 设计规范
下游文档：Unit G 实施计划
组件：#9 Python SDK + REST API
估算：1 周
依赖：Unit F (EvidencePackage Builder)

---

## 一、本 Unit 交付什么

Unit G 实现用户接口——Python SDK 和 REST API。SynthLang 为内部编译目标，v1 不直接对用户暴露。

交付物：
1. **Python SDK**：面向数据科学家的 Python 客户端
2. **REST API**：HTTP 接口，供其他服务调用
3. **C++ → Python 绑定**：pybind11
4. **API 文档**：OpenAPI/Swagger 规范

---

## 二、Python SDK

### 2.1 接口设计

```python
from synthgen import SynthGenClient, Column, RangeCheck

# 连接
client = SynthGenClient(base_url="http://localhost:8080")

# 定义 Schema
schema = client.define_type("sensor_log", columns={
    "timestamp": Column(DATETIME, order=True),
    "temperature": Column(FLOAT, range=[-50.0, 80.0]),
    "pressure": Column(FLOAT, range=[900.0, 1100.0]),
    "vibration": Column(FLOAT, range=[0.0, 10.0]),
    "status": Column(ENUM, values=["normal", "warning", "fault"])
})

# 导入数据
client.load_data("sensor_log", "/data/sensors.parquet")

# 定义约束（v1 仅矩形约束域）
constraint = client.define_constraint("safe_range", "sensor_log", [
    RangeCheck("temperature", min=-10, max=45),
    RangeCheck("pressure", min=980, max=1040)
])

# Explain：预览执行计划
plan = client.explain("sensor_log", constraints=["safe_range"])
# plan.execution_mode = "row_by_row"
# plan.path = "physics_sampling"
# plan.constraint_classification = {"value_range": 2, "inter_row": 0, "aggregate": 0}

# 生成
result = client.generate("sensor_log", constraints=["safe_range"], limit=1000)
# result.data: pd.DataFrame（1000 行合成数据）
# result.evidence: dict（EvidencePackage）

# 查看 tail_report
print(result.evidence["conservative_tail_report"]["data_grade"])
# "physics_guaranteed"
```

### 2.2 SDK 类定义

```python
class SynthGenClient:
    def __init__(self, base_url: str = "http://localhost:8080",
                 timeout: int = 30):
        self.base_url = base_url
        self.timeout = timeout
        self._session = requests.Session()

    def define_type(self, name: str, columns: Dict[str, Column]) -> SchemaRef:
        """定义数据类型 Schema"""
        ...

    def load_data(self, type_name: str, path: str,
                  mode: str = "strict") -> ImportResult:
        """导入 Parquet 数据"""
        ...

    def define_constraint(self, name: str, type_name: str,
                          checks: List[RangeCheck]) -> ConstraintRef:
        """定义约束卡片"""
        ...

    def explain(self, type_name: str,
                constraints: List[str]) -> ExplainResult:
        """预览执行计划"""
        ...

    def generate(self, type_name: str,
                 constraints: List[str],
                 limit: int,
                 seed: Optional[int] = None,
                 distribution: str = "uniform") -> GenerationResult:
        """生成合成数据"""
        ...

class Column:
    def __init__(self, type_: Type,
                 not_null: bool = False,
                 order: bool = False,
                 range: Optional[Tuple[float, float]] = None,
                 values: Optional[List[str]] = None):
        ...

class RangeCheck:
    def __init__(self, column: str,
                 min: Optional[float] = None,
                 max: Optional[float] = None):
        ...

class GenerationResult:
    data: pd.DataFrame
    evidence: Dict[str, Any]
    stats: GenerationStats
```

---

## 三、REST API

### 3.1 端点设计

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | /v1/types | 定义 Schema |
| POST | /v1/types/{name}/data | 导入数据 |
| POST | /v1/constraints | 定义约束 |
| POST | /v1/explain | 预览执行计划 |
| POST | /v1/generate | 生成合成数据 |
| GET | /v1/metrics | 可观测性指标 |
| GET | /v1/health | 健康检查 |

### 3.2 请求/响应格式

```http
POST /v1/generate
Content-Type: application/json

{
    "type_name": "sensor_log",
    "constraints": ["safe_range"],
    "limit": 1000,
    "seed": 42,
    "distribution": "uniform"
}
```

```http
HTTP/1.1 200 OK
Content-Type: application/json

{
    "data": {
        "format": "parquet",
        "url": "/v1/generate/abc123/data.parquet"
    },
    "evidence": {
        "schema_version": "v1",
        "schema_hash": "a3f2b8c1...",
        "exclusion_rate": 0.0,
        "data_grade": "physics_guaranteed",
        "row_count": 1000,
        "provenance": {...},
        "conservative_tail_report": {...},
        "audit_immutability": "not_applicable",
        "statistical_fidelity": "not_applicable",
        "drift_detection": "not_applicable",
        "constraint_type_breakdown": "not_applicable"
    },
    "stats": {
        "rows_generated": 1000,
        "elapsed_ms": 150,
        "distribution_used": "uniform"
    }
}
```

### 3.3 错误响应

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
    "error": {
        "code": "unsupported_in_v1",
        "message": "DURING constraints are not supported in v1. Supported from v2.",
        "component": "parser",
        "detail": {"line": 5, "column": 23}
    }
}
```

| HTTP 状态码 | 错误码 | 场景 |
|------------|--------|------|
| 400 | kInvalidArgument | 非法参数 |
| 400 | kUnsupportedInV1 | v1 不支持的语法 |
| 400 | kInvalidRange | 范围不合法 |
| 400 | kSchemaMismatch | Schema 不匹配 |
| 404 | kTypeNotFound | type 不存在 |
| 404 | kConstraintNotFound | 约束不存在 |
| 413 | kPayloadTooLarge | 请求体过大 |
| 500 | kInternalError | 内部错误 |
| 503 | kServiceUnavailable | 服务不可用 |
| 408 | kTimeout | 请求超时 |

---

## 四、C++ → Python 绑定

```cpp
// pybind11 绑定
PYBIND11_MODULE(synthgen, m) {
    m.doc() = "SynthGen Core Python SDK";

    py::class_<SynthGenClient>(m, "SynthGenClient")
        .def(py::init<const std::string&, int>(),
             py::arg("base_url") = "http://localhost:8080",
             py::arg("timeout") = 30)
        .def("define_type", &SynthGenClient::define_type)
        .def("load_data", &SynthGenClient::load_data)
        .def("define_constraint", &SynthGenClient::define_constraint)
        .def("explain", &SynthGenClient::explain)
        .def("generate", &SynthGenClient::generate);

    py::class_<Column>(m, "Column")
        .def(py::init<DataType, bool, bool,
             std::optional<std::pair<double, double>>,
             std::optional<std::vector<std::string>>>());

    py::class_<RangeCheck>(m, "RangeCheck")
        .def(py::init<const std::string&,
             std::optional<double>,
             std::optional<double>>());
}
```

---

## 五、错误处理

| 错误场景 | SDK 行为 | API 行为 |
|---------|---------|---------|
| 网络连接失败 | 抛出 ConnectionError | — |
| 请求超时 | 抛出 TimeoutError | 408 |
| 非法参数 | 抛出 ValueError | 400 |
| 服务端错误 | 抛出 ServerError | 500 |
| 资源不存在 | 抛出 NotFoundError | 404 |
| 请求体过大 | 抛出 PayloadTooLargeError | 413 |
| 服务不可用 | 抛出 ServiceUnavailableError | 503 |

---

## 六、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| Python SDK | 用户 | 数据科学家使用 |
| REST API | 其他服务 | 服务间调用 |
| /v1/metrics | 监控 | Prometheus 采集 |
| /v1/health | 负载均衡 | 健康检查 |

---

## 七、Unit G 验收标准

### 7.1 功能验收

- [ ] Python SDK 可安装（pip install）
- [ ] SDK define_type 工作正确
- [ ] SDK load_data 工作正确
- [ ] SDK define_constraint 工作正确
- [ ] SDK explain 工作正确
- [ ] SDK generate 工作正确
- [ ] REST API 所有端点可用
- [ ] API 返回正确 JSON
- [ ] EvidencePackage 在响应中完整
- [ ] 生成数据可下载为 Parquet

### 7.2 错误测试验收

- [ ] 非法参数 → 400 + 明确错误码
- [ ] v1 不支持语法 → 400 + kUnsupportedInV1
- [ ] 不存在的 type → 404
- [ ] 不存在的约束 → 404
- [ ] 请求体过大 → 413
- [ ] 服务端错误 → 500 + 审计日志
- [ ] 请求超时 → 408
- [ ] 网络断开 → SDK 抛出 ConnectionError
- [ ] SDK 超时 → SDK 抛出 TimeoutError
- [ ] 非法 JSON → 400 + kInvalidJson
- [ ] 缺少必填字段 → 400 + kMissingField

### 7.3 边界条件测试

- [ ] limit = 0 → 空结果
- [ ] limit = 1 → 1 行
- [ ] limit = 1000000 → 大结果
- [ ] 空约束列表 → 无约束生成
- [ ] 100 个约束 → 正常生成
- [ ] 超长 type_name（256字符）
- [ ] 特殊字符在 type_name 中
- [ ] 并发请求（100 个同时 generate）

### 7.4 端到端验收

- [ ] 完整流程：define_type → load_data → define_constraint → explain → generate
- [ ] 生成数据为有效 Parquet
- [ ] EvidencePackage 可通过 Schema 验证
- [ ] tail_report 包含偏差声明
- [ ] 固定 seed → 相同输出

### 7.5 脚手架验收

- [ ] /v1/metrics 返回 Prometheus 格式
- [ ] /v1/health 返回 200
- [ ] API 调用产生 Trace span
- [ ] 错误请求产生 error span

### 7.6 测试验收

- [ ] SDK 单元测试
- [ ] API 集成测试
- [ ] 端到端测试
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 至少 25 个测试用例
- [ ] CI 自动运行
