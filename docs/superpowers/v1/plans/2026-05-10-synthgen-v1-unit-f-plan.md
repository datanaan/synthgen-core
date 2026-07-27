SynthGen Core v1 Unit F 实施计划：EvidencePackage Builder v1
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit F 设计规范 v1.0
估算：1 周
依赖：Unit E (Validation + tail_report)

---

## 概述

Unit F 实现 EvidencePackage 构建器。组装生成结果、验证结果、tail_report 为完整的证据包，并自动验证 Schema 和诚实声明。

---

## Task 1：EvidencePackage 数据结构

**目标**：定义 EvidencePackage v1 的数据结构

### Step 1.1：结构定义

**做什么**：定义 EvidencePackageV1 及相关结构

**产出**：`src/engine/evidence/evidence_package.h`

**关键逻辑**：
- 所有字段使用 std::optional 或默认值
- JSON 序列化支持
- 字段适用性标注

**验收**：
- [ ] 结构定义完整
- [ ] 所有字段有默认值
- [ ] 可编译

### Step 1.2：JSON 序列化

**做什么**：实现 to_json / from_json

**产出**：`src/engine/evidence/evidence_package_json.h`, `src/engine/evidence/evidence_package_json.cpp`

**关键逻辑**：
- 使用 nlohmann/json 或 rapidjson
- 所有字段序列化/反序列化
- 特殊字符正确转义

**验收**：
- [ ] 序列化正确
- [ ] 反序列化正确
- [ ] 特殊字符正确转义
- [ ] 空字段正确处理

### Step 1.3：结构测试

**做什么**：编写数据结构单元测试

**产出**：`tests/unit/evidence_package_struct_test.cpp`

**测试用例**（至少 8 个）：
- 默认构造
- 字段赋值
- JSON 序列化
- JSON 反序列化
- 空字段序列化
- **错误测试**：非法 JSON 反序列化 → kDeserializationError
- **边界测试**：超长字符串序列化
- **边界测试**：特殊字符转义

**验收**：8+ 测试用例全通过

---

## Task 2：Schema 验证器

**目标**：实现 EvidencePackage 的自动验证

### Step 2.1：必填字段验证

**做什么**：验证所有必填字段存在且非空

**产出**：`src/engine/evidence/schema_validator.h`, `src/engine/evidence/schema_validator.cpp`

**关键逻辑**：
- 检查 12 个必填字段
- 检查字段类型
- 检查非空性

**验收**：
- [ ] 字段缺失 → kSchemaViolation
- [ ] 字段类型错误 → kSchemaViolation
- [ ] 空字符串 → kSchemaViolation
- [ ] NaN 数值 → kSchemaViolation

### Step 2.2：适用性标注验证

**做什么**：验证 not_applicable 字段正确标记

**关键逻辑**：
- audit_immutability 必须为 "not_applicable"
- statistical_fidelity 必须为 "not_applicable"
- drift_detection 必须为 "not_applicable"
- constraint_type_breakdown 必须为 "not_applicable"

**验收**：
- [ ] 错误标记 → kHonestyViolation
- [ ] 正确标记 → 通过

### Step 2.3：诚实声明验证

**做什么**：验证偏差声明和 data_grade

**关键逻辑**：
- epistemological_bias 必须为 "physical_first"
- tail_exclusion_statement 非空
- data_grade 必须为 "physics_guaranteed"
- exclusion_rate 必须为 0.0
- rows_failed_validation 必须为 0

**验收**：
- [ ] 偏差声明缺失 → kHonestyViolation
- [ ] data_grade 错误 → kHonestyViolation
- [ ] exclusion_rate != 0.0 → kConsistencyError

### Step 2.4：验证器测试

**做什么**：编写验证器单元测试

**产出**：`tests/unit/schema_validator_test.cpp`

**测试用例**（至少 16 个）：
- 完整正确的 EvidencePackage → 通过
- 字段缺失 → kSchemaViolation
- 字段类型错误 → kSchemaViolation
- audit_immutability 错误 → kHonestyViolation
- statistical_fidelity 错误 → kHonestyViolation
- drift_detection 错误 → kHonestyViolation
- constraint_type_breakdown 错误 → kHonestyViolation
- 偏差声明缺失 → kHonestyViolation
- data_grade 错误 → kHonestyViolation
- exclusion_rate != 0.0 → kConsistencyError
- rows_failed != 0 → kConsistencyError
- schema_hash 不匹配 → kHashMismatch
- **错误测试**：JSON 序列化失败 → kSerializationError
- **错误测试**：JSON 反序列化失败 → kDeserializationError
- **边界测试**：最小 EvidencePackage
- **边界测试**：最大 EvidencePackage

**验收**：16+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 3：EvidencePackageBuilder

**目标**：实现构建器

### Step 3.1：构建器实现

**做什么**：实现 EvidencePackageBuilder::build()

**产出**：`src/engine/evidence/evidence_package_builder.h`, `src/engine/evidence/evidence_package_builder.cpp`

**关键逻辑**：
- 收集所有输入
- 计算 schema_hash
- 组装 EvidencePackage
- 调用 validate_schema()

**验收**：
- [ ] 构建正确
- [ ] schema_hash 计算正确
- [ ] 自动验证通过

### Step 3.2：schema_hash 计算

**做什么**：实现 SHA256 哈希计算

**产出**：`src/common/hash.h`, `src/common/hash.cpp`

**关键逻辑**：
- Schema 序列化为字符串
- 计算 SHA256
- 返回 hex 字符串

**验收**：
- [ ] 相同 Schema → 相同 hash
- [ ] 不同 Schema → 不同 hash
- [ ] hash 格式正确（64 字符 hex）

### Step 3.3：构建器测试

**做什么**：编写构建器单元测试

**产出**：`tests/unit/evidence_package_builder_test.cpp`

**测试用例**（至少 12 个）：
- 正常构建
- schema_hash 正确
- 所有字段填充
- 诚实声明正确
- **错误测试**：验证失败 → build 返回错误
- **错误测试**：空 generation_result
- **错误测试**：空 validation_result
- **边界测试**：1 行生成
- **边界测试**：100000 行生成
- **边界测试**：1 个约束
- **边界测试**：100 个约束
- **边界测试**：0 个约束

**验收**：12+ 测试用例全通过

---

## Task 4：集成测试

**目标**：端到端验证 EvidencePackage

### Step 4.1：完整流程测试

**做什么**：从生成到 EvidencePackage 的完整流程

**产出**：`tests/integration/evidence_package_e2e_test.cpp`

**测试用例**（至少 8 个）：
- 完整流程：生成 → 验证 → tail_report → EvidencePackage
- EvidencePackage JSON 正确
- EvidencePackage 验证通过
- **错误测试**：诚实声明被篡改 → 验证失败
- **错误测试**：字段缺失 → 验证失败
- **边界测试**：最小生成
- **边界测试**：最大生成
- **边界测试**：JSON 往返序列化

**验收**：8+ 测试用例全通过

### Step 4.2：参考快照

**做什么**：生成参考 EvidencePackage 快照

**产出**：`tests/snapshots/evidence_package_v1_seed42.json`

**验收**：快照可读取，与重新生成的 EvidencePackage 一致

---

## Task 5：脚手架集成

**目标**：添加 Trace/Metrics

### Step 5.1：Trace span

- build → span(component="evidence", operation="build")
- validate_schema → span(component="evidence", operation="validate")

**验收**：每次操作产生 span

### Step 5.2：Metrics

```
evidence_package_total    — 构建次数
evidence_package_errors   — 验证失败次数
evidence_build_duration_ms — 构建耗时
```

**验收**：metrics 端点暴露上述指标

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 数据结构 | 3 | 0.2w | ⬜ |
| Task 2: Schema 验证 | 4 | 0.3w | ⬜ |
| Task 3: 构建器 | 3 | 0.3w | ⬜ |
| Task 4: 集成测试 | 2 | 0.15w | ⬜ |
| Task 5: 脚手架 | 2 | 0.05w | ⬜ |
| **合计** | **14** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| JSON 库依赖 | 使用 nlohmann/json（头文件库，零依赖） |
| SHA256 实现 | 使用 OpenSSL 或自研（简单哈希即可，非安全场景） |
| 诚实声明验证遗漏 | 对照路线图 v1.4 逐项检查 |
| Schema 验证与 v2+ 冲突 | v1 验证器只检查 v1 字段，v2 扩展时新增验证器 |
