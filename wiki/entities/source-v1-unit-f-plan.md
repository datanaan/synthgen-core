# v1 Unit F Plan — EvidencePackage Builder v1

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-f-plan.md
> 编译日期：2026-05-14

## 摘要

Unit F 实现 EvidencePackage 构建器，估算 1 周，依赖 Unit E（Validation + tail_report）。包含 5 个 Task、14 个步骤：EvidencePackage 数据结构定义（含 JSON 序列化/反序列化）、Schema 验证器（必填字段检查、适用性标注验证、诚实声明验证）、EvidencePackageBuilder（收集输入、计算 schema_hash、组装、自动验证）、集成测试（完整流程 + 参考快照）、脚手架集成。核心是组装生成结果、验证结果、tail_report 为完整的证据包。

## 关键要点

- EvidencePackage 包含 12 个必填字段，所有字段使用 std::optional 或默认值
- schema_hash 使用 SHA256 计算（Schema 序列化 -> SHA256 -> hex 字符串）
- 三层验证：必填字段验证、适用性标注验证（not_applicable 字段正确标记）、诚实声明验证
- v1 的 4 个 not_applicable 字段：audit_immutability、statistical_fidelity、drift_detection、constraint_type_breakdown
- JSON 序列化使用 nlohmann/json（头文件库，零依赖）

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `EvidencePackageV1` | `src/engine/evidence/evidence_package.h` | 数据结构定义 |
| JSON 序列化 | `src/engine/evidence/evidence_package_json.h/.cpp` | to_json / from_json |
| `SchemaValidator` | `src/engine/evidence/schema_validator.h/.cpp` | 自动验证（必填字段、适用性、诚实声明） |
| `EvidencePackageBuilder` | `src/engine/evidence/evidence_package_builder.h/.cpp` | 构建器入口 |
| SHA256 哈希 | `src/common/hash.h/.cpp` | Schema -> SHA256 hex |

### 三层验证

1. **必填字段验证**：12 个必填字段存在、类型正确、非空、非 NaN
2. **适用性标注验证**：v1 的 4 个 not_applicable 字段必须正确标记
3. **诚实声明验证**：epistemological_bias = "physical_first"、data_grade = "physics_guaranteed"、exclusion_rate = 0.0、rows_failed = 0

### schema_hash 计算

```
Schema 序列化为字符串 -> SHA256 -> 64 字符 hex 字符串
```

- 相同 Schema -> 相同 hash
- 不同 Schema -> 不同 hash
- 可使用 OpenSSL 或自研（非安全场景）

### 测试策略

- 数据结构测试 8+ 用例
- 验证器测试 16+ 用例（错误测试 >= 30%）
- 构建器测试 12+ 用例
- 集成测试 8+ 用例
- 参考快照：evidence_package_v1_seed42.json

## 提取的实体

- [[evidence-package]] — EvidencePackage（已存在）
- [[honesty-declaration]] — 诚实声明机制（已创建）
- [[exclusion-rate]] — 排除率（已创建）
- [[schema-hash]] — schema_hash SHA256 计算，用于 EvidencePackage 完整性校验（新实体）
- [[evidence-schema-validator]] — EvidencePackage 的三层自动验证器（新实体）
