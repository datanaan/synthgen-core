SynthGen Core v1 Unit E 实施计划：Validation + tail_report
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit E 设计规范 v1.0
估算：1 周
依赖：Unit D (Physics Engine v1)

---

## 概述

Unit E 实现值域约束验证器和 tail_report 构建器。验证器作为安全网逐行检查，tail_report 包含偏差声明和排除率统计。

---

## Task 1：ValueRangeValidator

**目标**：实现逐行值域验证

### Step 1.1：验证规则构建

**做什么**：从约束定义构建每列的验证规则

**产出**：`src/engine/constraint/validation_rule_builder.h`, `src/engine/constraint/validation_rule_builder.cpp`

**关键逻辑**：
- 解析约束（BETWEEN / > / < / >= / <=）
- 对每列，合并所有约束得到最终规则
- 检查约束引用的列是否存在

**验收**：
- [ ] BETWEEN 约束正确构建规则
- [ ] > / < 约束正确构建规则
- [ ] 多约束合并正确
- [ ] 约束引用不存在的列返回 kUndefinedColumn

### Step 1.2：逐行验证实现

**做什么**：实现 batch 的逐行验证

**产出**：`src/engine/constraint/value_range_validator.h`, `src/engine/constraint/value_range_validator.cpp`

**关键逻辑**：
- 遍历 batch 每行
- 对每列应用验证规则
- 记录失败（最多 100 个）
- 统计通过/失败行数

**验收**：
- [ ] 值在范围内 → 通过
- [ ] 值超出范围 → 失败
- [ ] 边界值（min, max）→ 通过
- [ ] 空 batch → 空结果
- [ ] 0 行 → 空结果

### Step 1.3：验证器测试

**做什么**：编写验证器单元测试

**产出**：`tests/unit/value_range_validator_test.cpp`

**测试用例**（至少 18 个）：
- 所有值在范围内 → 100% 通过
- 值超出上限 → 失败
- 值超出下限 → 失败
- 边界值（刚好等于 min）→ 通过
- 边界值（刚好等于 max）→ 通过
- 边界值（刚好 min-ε）→ 失败
- 边界值（刚好 max+ε）→ 失败
- 空 batch → 空结果
- 0 行 batch → 空结果
- 空约束列表 → 全部通过
- 多列约束 → 全部检查
- **错误测试**：约束引用不存在的列 → kUndefinedColumn
- **错误测试**：约束与 batch Schema 不匹配 → kSchemaMismatch
- **错误测试**：验证失败 101 行 → 只记录前 100 个
- **边界测试**：1 行 batch
- **边界测试**：100000 行 batch
- **边界测试**：范围宽度 = 0
- **边界测试**：范围宽度极大（DBL_MAX）

**验收**：18+ 测试用例全通过，错误测试占比 ≥ 30%

---

## Task 2：TailReportBuilder

**目标**：实现 tail_report 构建

### Step 2.1：排除率计算

**做什么**：实现每个约束的排除率计算

**产出**：`src/engine/evidence/exclusion_rate_calculator.h`, `src/engine/evidence/exclusion_rate_calculator.cpp`

**关键逻辑**：
- exclusion_rate = 1 - (constraint_range_width / schema_range_width)
- 处理 Schema 无范围声明的情况（使用默认值）
- 处理约束范围 = Schema 范围的情况（排除率 = 0）

**验收**：
- [ ] 排除率计算正确
- [ ] 约束范围 = Schema 范围 → 排除率 = 0
- [ ] 约束范围极小 → 排除率 ≈ 1
- [ ] Schema 无范围声明 → 使用默认值

### Step 2.2：TailReport 构建

**做什么**：实现 TailReportBuilder

**产出**：`src/engine/evidence/tail_report_builder.h`, `src/engine/evidence/tail_report_builder.cpp`

**关键逻辑**：
- 组装偏差声明
- 计算排除率
- 包含物理引擎统计
- 设置 data_grade

**验收**：
- [ ] tail_report 包含所有必须字段
- [ ] epistemological_bias = "physical_first"
- [ ] data_grade = "physics_guaranteed"
- [ ] 排除率统计正确

### Step 2.3：TailReport 测试

**做什么**：编写 tail_report 单元测试

**产出**：`tests/unit/tail_report_test.cpp`

**测试用例**（至少 12 个）：
- 正常构建
- 排除率计算（各种范围组合）
- 偏差声明完整
- data_grade 正确
- **错误测试**：空 generation_result
- **错误测试**：空 validation_result
- **边界测试**：排除率 = 0
- **边界测试**：排除率 ≈ 1
- **边界测试**：1 个约束
- **边界测试**：100 个约束
- **边界测试**：0 行生成
- **边界测试**：100000 行生成

**验收**：12+ 测试用例全通过

---

## Task 3：集成验证

**目标**：验证物理引擎 + 验证器 + tail_report 的完整流程

### Step 3.1：端到端验证测试

**做什么**：编写端到端测试

**产出**：`tests/integration/validation_pipeline_test.cpp`

**测试用例**（至少 10 个）：
- 完整流程：生成 → 验证 → tail_report
- 均匀分布生成 → 验证 100% 通过
- 高斯分布生成 → 验证 100% 通过（截断在采样时）
- 多约束 → 验证全部通过
- 无约束 → 验证全部通过
- **错误测试**：物理引擎 bug 模拟（注入越界值）→ 验证器发现
- **错误测试**：约束与生成数据不匹配 → 验证器发现
- **边界测试**：1 行生成 → 验证
- **边界测试**：100000 行生成 → 验证
- **边界测试**：1000 列 → 验证

**验收**：10+ 测试用例全通过

### Step 3.2：诚实声明验证

**做什么**：验证 tail_report 的诚实声明

**测试用例**：
- tail_report 包含 epistemological_bias
- tail_report 包含 tail_exclusion_statement
- tail_report 包含 data_grade
- 纯物理路径 rows_failed = 0

**验收**：所有诚实声明测试通过

---

## Task 4：脚手架集成

**目标**：添加 Trace/Metrics

### Step 4.1：Trace span

- validate_batch → span(component="validator", operation="validate_batch")
- build → span(component="tail_report", operation="build")

**验收**：每次操作产生 span

### Step 4.2：Metrics

```
validation_total      — 验证调用次数
validation_passed     — 验证通过次数
validation_failed     — 验证失败次数
tail_report_total     — tail_report 构建次数
```

**验收**：metrics 端点暴露上述指标

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: Validator | 3 | 0.4w | ⬜ |
| Task 2: TailReport | 3 | 0.3w | ⬜ |
| Task 3: 集成验证 | 2 | 0.2w | ⬜ |
| Task 4: 脚手架 | 2 | 0.1w | ⬜ |
| **合计** | **10** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 验证器性能（大 batch） | 逐行检查是 O(n)，100000 行 < 100ms 可接受 |
| tail_report 偏差声明遗漏 | 对照路线图 v1.4 诚实声明逐项检查 |
| 排除率计算精度 | 使用 double，边界情况特殊处理 |
