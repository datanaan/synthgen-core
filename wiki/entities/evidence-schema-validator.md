# Evidence Schema 验证器

> 类型：组件

## 定义

EvidencePackage 的三层自动验证组件。在 EvidencePackage 构建完成后自动运行，确保证据包的完整性和诚实性。

## 三层验证

### 第一层：必填字段验证

检查 12 个必填字段：
- 字段存在
- 字段类型正确
- 非空
- 数值字段非 NaN

错误码：`kSchemaViolation`

### 第二层：适用性标注验证

v1 的 4 个 not_applicable 字段必须正确标记：
- audit_immutability = "not_applicable"
- statistical_fidelity = "not_applicable"
- drift_detection = "not_applicable"
- constraint_type_breakdown = "not_applicable"

错误码：`kHonestyViolation`

### 第三层：诚实声明验证

- epistemological_bias = "physical_first"
- tail_exclusion_statement 非空
- data_grade = "physics_guaranteed"
- exclusion_rate = 0.0
- rows_failed_validation = 0

错误码：`kHonestyViolation`、`kConsistencyError`

## 额外检查

- schema_hash 一致性 -> `kHashMismatch`
- JSON 序列化/反序列化 -> `kSerializationError`、`kDeserializationError`

## 文件位置

`src/engine/evidence/schema_validator.h`, `src/engine/evidence/schema_validator.cpp`

## 关联实体

- [[evidence-package]] — 被验证的对象
- [[honesty-declaration]] — 第二、三层验证诚实声明
- [[schema-hash]] — 验证 schema_hash 一致性
