SynthGen Core v1 阶段设计规范
文档性质：v1 阶段级约束——v1 全部 Unit 的共同基础
版本：v1.0
日期：2026-05-10
适用范围：SynthGen Core v1 最小可运行
上游文档：整体设计规范 v1.0、路线图 v1.4、工程框架 v0.4、工程执行守则 v1.0
下游文档：Unit A-I 的各 spec 和 plan

---

## 一、v1 产品故事

定义数据域，导入真实数据，在矩形约束域内物理采样，获得物理合法的合成数据和诚实的证据包。

**v1 能做的**：
- SynthLang 核心语法解析（DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT / GENERATE TABLE）
- 类型系统 + Schema DDL（含 ORDER 预留）
- Parquet 文件导入 + Schema 校验
- 基础存储引擎（对象存储 + Parquet + 元数据层 v1）
- 矩形约束域内物理采样（均匀/高斯 + 种子控制）
- 值域约束验证（纯物理路径下 100% 通过）
- tail_report v1（值域排除率 + 偏差声明 + data_grade）
- EvidencePackage v1（含字段适用性标注）
- Python SDK + REST API

**v1 做不了的（诚实声明）**：
- ❌ 数据驱动能力 → statistical_fidelity: not_applicable
- ❌ 漂移检测 → drift_detection: not_applicable
- ❌ 审计不可变保证 → audit_immutability: not_applicable
- ❌ 复合约束域（DURING/WHEN 产生的非矩形空间）→ 传入时返回 unsupported_in_v1
- ❌ 行间约束、聚合约束 → v2
- ❌ 执行路由器、退化路径 → v2

---

## 二、v1 依赖图

```
Unit A: Parser + Type System ──────────┐
    │ #1 Parser (1.5w)                  │
    │ #2 Type System/Schema DDL (1w)    │
    └──────────────────┬────────────────┤
                       │                │
Unit B: Storage Engine │   Unit D: Physics Engine v1
    │ #4 Storage (1.5w)│       │ #5 矩形域采样 (1.5w)
    └──────┬───────────┘       └──────┬──────┘
           │                          │
Unit C: Data Import                   │
    │ #3 LOAD DATA (0.5w)             │
    │ ← 依赖 A + B                    │
    └──────────────────────────────────┤
                                      │
                           Unit E: Validation + Reporting
                               │ #6 值域验证器 (0.5w)
                               │ #7 tail_report (0.5w)
                               │ ← 依赖 D
                               └──────┬──────┘
                                      │
                           Unit F: EvidencePackage
                               │ #8 构建器 v1 (1w)
                               │ ← 依赖 E
                               └──────┬──────┘
                                      │
                           Unit G: SDK + REST API
                               │ #9 用户接口 (1w)
                               │ ← 依赖 F
                               └──────┘

Unit H: Scaffold v1 ──────── 与 C-G 并行
    │ Explain/Trace/Observability/Test/CI (1.5w)

Unit I: Tool Line v1 ────── v1 第4周起
    │ 模板引擎 v0.1 + 测试辅助库 v0.1 (0.5w)
```

### 2.1 开发波次

| 波次 | 时间 | 活跃 Unit | 里程碑 |
|------|------|----------|---------|
| Wave 1 | 第1-3周 | A, B, D | Parser 能解析 DEFINE TYPE + CONSTRAINT；存储能读写 Parquet；物理引擎能矩形域采样 |
| Wave 2 | 第3-5周 | C, E, H | 数据能导入；验证器能检查值域；tail_report 能生成；脚手架就位 |
| Wave 3 | 第5-7周 | F, I | EvidencePackage 完整构建；模板引擎 v0.1 可用 |
| Wave 4 | 第7-9周 | G | SDK 可端到端调用；v1 交付 |

---

## 三、v1 组件接口定义

### 3.1 #1 SynthLang Parser

**输入**：SynthLang 源文本
**输出**：AST（抽象语法树）

```cpp
namespace synthgen::parser {

// v1 支持的语句类型
enum class StatementType {
    kDefineType,
    kLoadData,
    kDefineConstraint,  // v1 仅值域 BETWEEN/MIN/MAX
    kGenerateTable,
};

// Parse 结果
struct ParseResult {
    std::vector<std::unique_ptr<Statement>> statements;
    std::vector<ParseError> errors;
};

class Parser {
public:
    Result<ParseResult> parse(const std::string& source) const;

    // v1 限制检查
    // DURING/WHEN → 返回 unsupported_in_v1 错误
    // 行间约束语法 → 返回 unsupported_in_v1 错误
    // 聚合约束语法 → 返回 unsupported_in_v1 错误
};

}  // namespace synthgen::parser
```

**v1 语法子集**：
```
DEFINE TYPE <name> {
    <column>: <type> [NOT NULL] [ORDER],
    <column>: FLOAT [<min>, <max>],
    <column>: ENUM(<values>),
    ...
};
LOAD DATA INTO <type_name> FROM '<path>';
DEFINE CONSTRAINT <name> ON <type_name> {
    <column> BETWEEN <min> AND <max>,
    <column> > <value>,
    <column> < <value>,
    ...
};
GENERATE TABLE <name> FROM <type_name>
    WITH CONSTRAINTS <constraint_name>
    LIMIT <n>;
```

### 3.2 #2 Type System + Schema DDL

**输入**：Parser 产生的 DEFINE TYPE AST
**输出**：Schema 对象

```cpp
namespace synthgen::schema {

enum class DataType { kFloat, kInt, kDatetime, kString, kEnum };

struct ColumnDef {
    std::string name;
    DataType type;
    bool not_null = false;
    bool is_order = false;          // v1 预留，v2 使用
    std::optional<double> range_min; // 值域范围声明
    std::optional<double> range_max;
    std::vector<std::string> enum_values; // ENUM 类型
};

struct Schema {
    std::string type_name;
    std::vector<ColumnDef> columns;
    std::vector<std::string> order_columns;  // ORDER 声明的列

    // 校验
    Result<void> validate() const;

    // 查询
    std::optional<ColumnDef> find_column(const std::string& name) const;
};

class SchemaRegistry {
public:
    Result<void> register_schema(const Schema& schema);
    Result<const Schema*> get_schema(const std::string& type_name) const;
};

}  // namespace synthgen::schema
```

### 3.3 #3 Data Import (LOAD DATA)

**输入**：Schema + Parquet 文件路径
**输出**：基表层中的数据

```cpp
namespace synthgen::storage {

struct ImportResult {
    int64_t rows_imported;
    std::string table_id;
    std::string snapshot_id;
};

class DataImporter {
public:
    Result<ImportResult> import(const Schema& schema,
                                 const std::string& parquet_path,
                                 StorageBackend& storage);
    // Schema 校验：列名/类型/范围与 Parquet schema 对齐
    // 不匹配 → 错误，不静默跳过
};

}  // namespace synthgen::storage
```

### 3.4 #4 Storage Engine

```cpp
namespace synthgen::storage {

// 基础存储抽象接口
class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    // 写入
    virtual Result<SnapshotRef> append(const std::string& table_id,
                                        const ArrowBatch& batch) = 0;

    // 读取
    virtual Result<ArrowBatchIterator> scan(const std::string& table_id,
                                             const std::string& snapshot_id,
                                             const std::vector<std::string>& columns,
                                             const std::optional<Predicate>& pred) = 0;

    // 版本
    virtual Result<std::vector<VersionMeta>> list_versions(
        const std::string& table_id) = 0;
    virtual Result<SnapshotRef> get_snapshot(const std::string& table_id,
                                              const std::string& version_tag) = 0;

    // v1 元数据层
    virtual Result<void> register_table(const std::string& table_id,
                                         const Schema& schema) = 0;
};

// v1 默认后端：对象存储 + Parquet + 自研元数据层
class ObjectStoreBackend : public StorageBackend {
    // 实现略
};

}  // namespace synthgen::storage
```

### 3.5 #5 Physics Engine v1 (矩形域采样)

```cpp
namespace synthgen::engine::physics {

struct GenerationRequest {
    const Schema& schema;
    std::vector<ConstraintDef> constraints;  // v1 仅值域
    int64_t limit;
    uint64_t seed;
    std::string distribution;  // "uniform" | "gaussian"
};

struct GenerationResult {
    ArrowBatch data;
    GenerationStats stats;
    // stats.rows_generated, stats.exclusion_rate (v1 应为 0), stats.elapsed_ms
};

class RectangularSampler {
public:
    explicit RectangularSampler(const Schema& schema);

    Result<GenerationResult> generate(const GenerationRequest& request);

    // Explain
    ExplainInfo explain(const GenerationRequest& request) const;

private:
    // 矩形域采样：在 BETWEEN/MIN/MAX 定义的超矩形内采样
    // 支持 uniform 和 gaussian 分布
    // 种子控制：seed → batch_seed = hash(seed + batch_index)
};

}  // namespace synthgen::engine::physics
```

### 3.6 #6 Value Range Validator

```cpp
namespace synthgen::engine::constraint {

struct ValidationResult {
    int64_t rows_checked;
    int64_t rows_passed;
    int64_t rows_failed;  // v1 纯物理路径应为 0
    double pass_rate;      // v1 应为 1.0
    std::vector<ValidationFailure> failures;  // 最多记录前 100 个
};

class ValueRangeValidator {
public:
    explicit ValueRangeValidator(const Schema& schema,
                                  const std::vector<ConstraintDef>& constraints);

    Result<ValidationResult> validate_batch(const ArrowBatch& batch);

    // Explain
    ExplainInfo explain() const;

    // 安全网：即使物理引擎保证在值域内，验证器仍逐行检查
    // 发现越界 → 记录到 tail_report（不应发生，但安全网不删）
};

}  // namespace synthgen::engine::constraint
```

### 3.7 #7 tail_report v1

```cpp
namespace synthgen::engine::evidence {

struct TailReportV1 {
    // 认识论偏差声明（必须）
    std::string epistemological_bias = "physical_first";

    // 尾部排除声明（必须）
    std::string tail_exclusion_statement =
        "Tail events systematically excluded by value range constraints";

    // 按约束的排除率
    std::vector<ConstraintExclusionRate> exclusion_rate_by_constraint;

    // data_grade
    std::string data_grade = "physics_guaranteed";
};

struct ConstraintExclusionRate {
    std::string constraint_name;
    double rate;  // v1 纯物理路径应为 0.0
};

class TailReportBuilder {
public:
    Result<TailReportV1> build(const ValidationResult& validation_result,
                                const GenerationRequest& request);
};

}  // namespace synthgen::engine::evidence
```

### 3.8 #8 EvidencePackage Builder v1

```cpp
namespace synthgen::engine::evidence {

struct EvidencePackageV1 {
    // === always 字段 ===
    std::string schema_version = "v1";
    std::string schema_hash;
    ConstraintSummary constraint_summary;
    double exclusion_rate;
    std::string data_grade = "physics_guaranteed";
    int64_t row_count;
    ProvenanceV1 provenance;
    ConservativeTailReport conservative_tail_report;

    // === not_applicable 字段 ===
    std::string audit_immutability = "not_applicable";
    std::string statistical_fidelity = "not_applicable";
    std::string drift_detection = "not_applicable";
    std::string constraint_type_breakdown = "not_applicable";
};

struct ProvenanceV1 {
    std::string data_source;
    std::vector<std::string> constraints;
    GenerationParams generation_params;
    std::vector<TraceSpan> trace_spans;
    std::string generator_identity = "physics_sampler";
};

class EvidencePackageBuilder {
public:
    Result<EvidencePackageV1> build(const GenerationResult& generation_result,
                                     const ValidationResult& validation_result,
                                     const TailReportV1& tail_report,
                                     const ProvenanceV1& provenance);

    // Schema 自动验证：构建后立即校验字段完整性
    Result<void> validate_schema(const EvidencePackageV1& pkg) const;
};

}  // namespace synthgen::engine::evidence
```

### 3.9 #9 Python SDK + REST API

```python
# Python SDK v1（用户接口）
from synthgen import SynthGenClient, Column, RangeCheck

client = SynthGenClient(base_url="http://localhost:8080")

# 定义 Schema
schema = client.define_type("sensor_log", columns={
    "timestamp": Column(DATETIME, order=True),
    "temperature": Column(FLOAT, range=[-50.0, 80.0]),
    "pressure": Column(FLOAT, range=[900.0, 1100.0])
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

# 生成
result = client.generate("sensor_log", constraints=["safe_range"], limit=1000)
# result.data: 合成数据
# result.evidence: EvidencePackage
```

---

## 四、v1 诚实声明（必须传递）

以下声明在每个 v1 的 EvidencePackage 中必须体现：

| 声明 | EvidencePackage 体现 | 代码实现位置 |
|------|---------------------|------------|
| 物理优先认识论偏差 | conservative_tail_report.epistemological_bias = "physical_first" | #7 tail_report |
| 尾部事件系统性排除 | conservative_tail_report.tail_exclusion_statement | #7 tail_report |
| 条件保证 | data_grade = "physics_guaranteed" | #8 EvidencePackage |
| 无审计不可变保证 | audit_immutability = "not_applicable" | #8 EvidencePackage |
| 无数据驱动能力 | statistical_fidelity = "not_applicable" | #8 EvidencePackage |
| 无漂移检测 | drift_detection = "not_applicable" | #8 EvidencePackage |
| 无约束类型分类 | constraint_type_breakdown = "not_applicable" | #8 EvidencePackage |
| 仅矩形约束域 | constraint_summary.type = "value_range" | #8 EvidencePackage |
| DURING/WHEN 不支持 | Parser 返回 unsupported_in_v1 | #1 Parser |

---

## 五、v1 错误测试验收标准

**Parser 错误测试**：
- [ ] 空输入返回 kSyntaxError
- [ ] 非法字符返回 kSyntaxError，Lexer 继续分析后续内容
- [ ] 超长标识符（>1024字符）返回 kSyntaxError
- [ ] 未闭合的字符串返回 kSyntaxError
- [ ] 数字溢出（FLOAT > DBL_MAX）返回 kInvalidRange
- [ ] 重复列名返回 kDuplicateColumnName
- [ ] 不存在的 type 引用返回 kUndefinedType
- [ ] DURING/WHEN/行间/聚合语法返回 kUnsupportedInV1，错误消息含版本提示
- [ ] 非数值列施加 BETWEEN 返回 kTypeMismatch

**存储错误测试**：
- [ ] 文件不存在返回 kTableNotFound
- [ ] 目录无写入权限返回 kWriteFailed
- [ ] 损坏的 Parquet 文件返回 kDataCorruption
- [ ] 空 Parquet 文件（0行）正确处理
- [ ] 超大 Parquet 文件（>内存）流式读取不崩溃
- [ ] 重复注册表返回 kTableAlreadyExists
- [ ] 未注册表执行 append/scan 返回 kTableNotFound
- [ ] 元数据文件损坏后 reload 返回 kDataCorruption

**物理引擎错误测试**：
- [ ] 空约束列表正确处理（无约束时返回全 Schema 范围采样）
- [ ] limit = 0 返回空结果
- [ ] limit < 0 返回 kInvalidArgument
- [ ] 约束范围与 Schema 声明范围冲突返回 kInvalidRange
- [ ] 不存在的 distribution 参数返回 kInvalidArgument
- [ ] 空 Schema 返回 kInvalidArgument
- [ ] 非法种子值（如 seed = UINT64_MAX 的特殊处理）行为确定

**验证器错误测试**：
- [ ] 空批次验证返回空结果（0行检查，通过率为空）
- [ ] 约束与 Schema 列不匹配返回 kSchemaMismatch
- [ ] 验证失败行超过 100 条时只记录前 100 条

**SDK/API 错误测试**：
- [ ] 非法参数返回 400 Bad Request + 明确错误码
- [ ] 不存在的资源返回 404 Not Found
- [ ] 服务端内部错误返回 500 Internal Server Error + 审计日志记录
- [ ] 请求超时返回 408 Request Timeout
- [ ] 超大请求体（>10MB）返回 413 Payload Too Large
- [ ] 非法 JSON 格式返回 400 + kInvalidJson

---

## 六、v1 脚手架验收标准（与功能验收同等地位）

| 脚手架 | v1 交付 | 验收标准 |
|--------|---------|---------|
| Explain 最小版 | 约束分类结果 + 执行模式 + 路由决策 | `client.explain()` 返回 execution_mode + path + constraint_classification |
| Trace 最小版 | 每个组件产生 span，写入 provenance | EvidencePackage.provenance.trace_spans 非空且完整 |
| 可观测性最小版 | /metrics 端点 | `curl /metrics` 返回 generation_throughput + request_latency_ms + memory_usage_bytes |
| 确定性测试框架 | seed 固定 + 参考快照 | 固定 seed 输出与快照逐行比对一致 |
| CI/CD 基础设施 | 每次 PR 触发 | PR 提交 → Parser 单元测试 + 端到端生成测试自动运行 |

---

## 六、v1 工具验收标准

| 工具 | 验收标准 |
|------|---------|
| 组件模板引擎 v0.1 | 从 #5/#6 提炼模板，生成 #8 骨架代码能通过编译和基础 CI |
| 测试辅助库 v0.1 | TEST_RANGE_VALIDATION 宏：min-ε 和 max+ε 测试失败，min 和 max 测试通过 |

---

## 七、v1 盲区 deadline（来自工程执行守则）

| 盲区 | deadline | 不做的后果 |
|------|---------|----------|
| 约束卡片生产者缺位 | v1 交付前，至少有一份 DomainPack 模板 | 物理合法性验证空转 |
| Twin 集成测试 | v1 交付后 2 周内 | v4 回修 Schema 成本 10x |
| 真实数据冷启动 | v2 开发启动前 | v2 后筛选预估纸上谈兵 |

---

## 八、v1 Unit 分配总表

| Unit | 名称 | 组件 | 估算 | 依赖 | 波次 |
|------|------|------|------|------|------|
| A | Parser + Type System | #1, #2 | 2.5w | 无 | W1 |
| B | Storage Engine | #4 | 1.5w | 无 | W1 |
| C | Data Import | #3 | 0.5w | A+B | W2 |
| D | Physics Engine v1 | #5 | 1.5w | A | W1 |
| E | Validation + Reporting | #6, #7 | 1w | D | W2 |
| F | EvidencePackage | #8 | 1w | E | W3 |
| G | SDK + REST API | #9 | 1w | F | W4 |
| H | Scaffold v1 | 5项脚手架 | 1.5w | 与C-G并行 | W2 |
| I | Tool Line v1 | 2项工具 | 0.5w | W4+ | W3 |

**v1 总估算**：8.5-9.5 周（含脚手架 1.5w + 工具 0.5w）
