SynthGen Core v1 Unit F 设计规范：EvidencePackage Builder v1
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v1 阶段设计规范 v1.0、整体设计规范 v1.0、Unit E 设计规范
下游文档：Unit F 实施计划
组件：#8 EvidencePackage 构建器 v1
估算：1 周
依赖：Unit E (Validation + tail_report)

---

## 一、本 Unit 交付什么

Unit F 实现 EvidencePackage 构建器——将生成结果、验证结果、tail_report 组装成完整的证据包。

**核心职责**：
1. **组装**：收集所有组件的输出，构建 EvidencePackage
2. **Schema 验证**：构建后自动验证字段完整性
3. **诚实声明传递**：确保所有 v1 的诚实声明正确填充
4. **字段适用性标注**：不适用的字段标记为 not_applicable

---

## 二、EvidencePackage v1 Schema

### 2.1 完整结构

```json
{
  "$schema": "EvidencePackage/v1",
  "schema_version": "v1",
  "schema_hash": "SHA256(schema_definition)",
  "constraint_summary": {
    "type": "value_range",
    "details": [
      {"column": "temperature", "min": -10.0, "max": 45.0},
      {"column": "pressure", "min": 980.0, "max": 1040.0}
    ]
  },
  "exclusion_rate": 0.0,
  "data_grade": "physics_guaranteed",
  "row_count": 1000,
  "provenance": {
    "data_source": "/data/sensors.parquet",
    "constraints": ["safe_range"],
    "generation_params": {
      "seed": 42,
      "distribution": "uniform",
      "limit": 1000,
      "batch_size": 1000
    },
    "trace_spans": [
      {
        "trace_id": "evp_abc123",
        "span_id": "span_001",
        "component": "parser",
        "operation": "parse",
        "status": "ok"
      },
      {
        "trace_id": "evp_abc123",
        "span_id": "span_002",
        "component": "physics_engine",
        "operation": "generate",
        "status": "ok"
      }
    ],
    "generator_identity": "physics_sampler"
  },
  "conservative_tail_report": {
    "epistemological_bias": "physical_first",
    "tail_exclusion_statement": "Tail events systematically excluded by value range constraints",
    "exclusion_rate_by_constraint": [
      {"constraint": "safe_range", "rate": 0.0}
    ],
    "data_grade": "physics_guaranteed",
    "rows_generated": 1000,
    "rows_validated": 1000,
    "rows_failed_validation": 0,
    "distribution_used": "uniform",
    "seed_used": 42
  },
  "audit_immutability": "not_applicable",
  "statistical_fidelity": "not_applicable",
  "drift_detection": "not_applicable",
  "constraint_type_breakdown": "not_applicable"
}
```

### 2.2 字段适用性标注

| 字段 | 适用性 | v1 状态 | 说明 |
|------|--------|---------|------|
| schema_version | always | ✅ "v1" | — |
| schema_hash | always | ✅ 填充 | — |
| constraint_summary | always | ✅ 仅值域 | — |
| exclusion_rate | always | ✅ 0.0 | 纯物理路径 |
| data_grade | always | ✅ "physics_guaranteed" | — |
| row_count | always | ✅ 填充 | — |
| provenance | always | ✅ 基础版 | 含 trace_spans |
| conservative_tail_report | always | ✅ 填充 | 含偏差声明 |
| audit_immutability | always | ⬜ "not_applicable" | v1 无审计 |
| statistical_fidelity | data_engaged | ⬜ "not_applicable" | v1 无数据驱动 |
| drift_detection | drift_available | ⬜ "not_applicable" | v1 无漂移检测 |
| constraint_type_breakdown | aggregation_present | ⬜ "not_applicable" | v1 仅有值域 |

---

## 三、EvidencePackageBuilder 接口

```cpp
namespace synthgen::engine::evidence {

class EvidencePackageBuilder {
public:
    // 构建 EvidencePackage
    Result<EvidencePackageV1> build(
        const GenerationResult& generation_result,
        const ValidationResult& validation_result,
        const TailReportV1& tail_report,
        const ProvenanceV1& provenance);

    // Schema 自动验证
    Result<void> validate_schema(const EvidencePackageV1& pkg) const;

    // 序列化
    Result<std::string> to_json(const EvidencePackageV1& pkg) const;
    Result<EvidencePackageV1> from_json(const std::string& json) const;

    // 计算 schema_hash
    static std::string compute_schema_hash(const Schema& schema);

private:
    // 验证必填字段
    Result<void> validate_required_fields(const EvidencePackageV1& pkg) const;

    // 验证适用性标注
    Result<void> validate_applicability(const EvidencePackageV1& pkg) const;

    // 验证诚实声明
    Result<void> validate_honesty(const EvidencePackageV1& pkg) const;
};

}  // namespace synthgen::engine::evidence
```

---

## 四、Schema 验证规则

### 4.1 必填字段检查

```cpp
// 所有 "always" 字段必须存在且非空
REQUIRED_FIELDS = [
    "schema_version",
    "schema_hash",
    "constraint_summary",
    "exclusion_rate",
    "data_grade",
    "row_count",
    "provenance",
    "conservative_tail_report",
    "audit_immutability",
    "statistical_fidelity",
    "drift_detection",
    "constraint_type_breakdown"
]

// 每个字段检查：
// - 存在性：字段必须存在
// - 类型正确性：值类型符合预期
// - 非空性：字符串非空，数值非 NaN
```

### 4.2 适用性标注验证

```cpp
// v1 的 not_applicable 字段必须正确标记：
if pkg.audit_immutability != "not_applicable":
    return kHonestyViolation  // v1 必须声明无审计

if pkg.statistical_fidelity != "not_applicable":
    return kHonestyViolation  // v1 必须声明无数据驱动

if pkg.drift_detection != "not_applicable":
    return kHonestyViolation  // v1 必须声明无漂移检测

if pkg.constraint_type_breakdown != "not_applicable":
    return kHonestyViolation  // v1 必须声明无约束分类
```

### 4.3 诚实声明验证

```cpp
// 偏差声明必须完整：
if tail_report.epistemological_bias != "physical_first":
    return kHonestyViolation

if tail_report.tail_exclusion_statement.empty():
    return kHonestyViolation

if data_grade != "physics_guaranteed":
    return kHonestyViolation

// 纯物理路径下：
if exclusion_rate != 0.0:
    return kConsistencyError  // v1 纯物理应为 0

if rows_failed_validation != 0:
    return kConsistencyError  // v1 应为 0
```

---

## 五、错误处理

| 错误场景 | 错误码 | 行为 |
|---------|--------|------|
| 必填字段缺失 | kSchemaViolation | 失败 |
| 字段类型错误 | kSchemaViolation | 失败 |
| not_applicable 字段错误标记 | kHonestyViolation | 失败 |
| 偏差声明缺失 | kHonestyViolation | 失败 |
| data_grade 错误 | kHonestyViolation | 失败 |
| exclusion_rate 不一致 | kConsistencyError | 失败 |
| rows_failed 不一致 | kConsistencyError | 失败 |
| JSON 序列化失败 | kSerializationError | 失败 |
| JSON 反序列化失败 | kDeserializationError | 失败 |
| schema_hash 不匹配 | kHashMismatch | 失败 |

---

## 六、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `EvidencePackageBuilder::build()` | Unit G (SDK) | 用户获取证据包 |
| `EvidencePackageBuilder::to_json()` | Unit G (REST API) | HTTP 响应 |
| `EvidencePackageBuilder::validate_schema()` | Unit H (CI) | 每次生成后自动验证 |
| `EvidencePackageV1` | Unit H (测试框架) | 参考快照比对 |

---

## 七、Unit F 验收标准

### 7.1 功能验收

- [ ] EvidencePackage 包含所有必填字段
- [ ] schema_hash 计算正确（SHA256）
- [ ] constraint_summary 仅含值域约束
- [ ] exclusion_rate = 0.0（纯物理路径）
- [ ] data_grade = "physics_guaranteed"
- [ ] provenance 包含 trace_spans
- [ ] conservative_tail_report 包含偏差声明
- [ ] audit_immutability = "not_applicable"
- [ ] statistical_fidelity = "not_applicable"
- [ ] drift_detection = "not_applicable"
- [ ] constraint_type_breakdown = "not_applicable"
- [ ] JSON 序列化/反序列化正确

### 7.2 错误测试验收

- [ ] 必填字段缺失 → kSchemaViolation
- [ ] 字段类型错误 → kSchemaViolation
- [ ] audit_immutability 错误标记 → kHonestyViolation
- [ ] statistical_fidelity 错误标记 → kHonestyViolation
- [ ] drift_detection 错误标记 → kHonestyViolation
- [ ] constraint_type_breakdown 错误标记 → kHonestyViolation
- [ ] 偏差声明缺失 → kHonestyViolation
- [ ] data_grade 错误 → kHonestyViolation
- [ ] exclusion_rate != 0.0 → kConsistencyError
- [ ] rows_failed != 0 → kConsistencyError
- [ ] JSON 序列化失败 → kSerializationError
- [ ] JSON 反序列化失败 → kDeserializationError
- [ ] schema_hash 不匹配 → kHashMismatch

### 7.3 边界条件测试

- [ ] 最小 EvidencePackage（1 行，1 列，1 约束）
- [ ] 最大 EvidencePackage（100000 行，1000 列，100 约束）
- [ ] 空 constraint_summary（无约束）
- [ ] 空 provenance.trace_spans
- [ ] 超长字符串字段（>10KB）
- [ ] 特殊字符在 JSON 中正确转义

### 7.4 诚实声明验收

- [ ] 所有 not_applicable 字段正确标记
- [ ] 偏差声明完整且不可为空
- [ ] data_grade 不可被篡改
- [ ] 验证失败时 EvidencePackage 标记 failed: true

### 7.5 脚手架验收

- [ ] build 产生 Trace span
- [ ] validate_schema 产生 Trace span
- [ ] /metrics 暴露 evidence_package_total / evidence_package_errors

### 7.6 测试验收

- [ ] 单元测试：构建 + 验证 + 序列化
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 每个 ErrorCode 至少 1 个测试用例触发
- [ ] 至少 25 个测试用例
- [ ] CI 自动运行
