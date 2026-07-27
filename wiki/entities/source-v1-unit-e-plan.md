# v1 Unit E Plan — Validation + tail_report

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-e-plan.md
> 编译日期：2026-05-14

## 摘要

Unit E 实现值域约束验证器（ValueRangeValidator）和 tail_report 构建器（TailReportBuilder），估算 1 周，依赖 Unit D（Physics Engine）。包含 4 个 Task、10 个步骤：ValueRangeValidator（从约束构建验证规则、逐行验证 batch）、TailReportBuilder（排除率计算、偏差声明组装、data_grade 设置）、集成验证（物理引擎 + 验证器 + tail_report 完整流程）、脚手架集成。验证器作为安全网逐行检查，tail_report 包含诚实声明。

## 关键要点

- ValueRangeValidator 是安全网：即使物理引擎已保证采样在范围内，验证器仍逐行检查
- 排除率公式：exclusion_rate = 1 - (constraint_range_width / schema_range_width)
- tail_report 的三项诚实声明：epistemological_bias = "physical_first"、data_grade = "physics_guaranteed"、纯物理路径 rows_failed = 0
- 验证失败最多记录 100 个（避免内存爆炸）
- 诚实声明验证是核心测试场景

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `ValidationRuleBuilder` | `src/engine/constraint/validation_rule_builder.h/.cpp` | 从约束定义构建每列验证规则 |
| `ValueRangeValidator` | `src/engine/constraint/value_range_validator.h/.cpp` | 逐行值域验证，统计通过/失败 |
| `ExclusionRateCalculator` | `src/engine/evidence/exclusion_rate_calculator.h/.cpp` | 排除率计算 |
| `TailReportBuilder` | `src/engine/evidence/tail_report_builder.h/.cpp` | tail_report 构建 |

### 验证规则构建

- 解析约束（BETWEEN / > / < / >= / <=）
- 对每列合并所有约束得到最终规则
- 约束引用不存在的列 -> kUndefinedColumn

### 排除率计算

```
exclusion_rate = 1 - (constraint_range_width / schema_range_width)
```

- 约束范围 = Schema 范围 -> 排除率 = 0
- 约束范围极小 -> 排除率接近 1
- Schema 无范围声明 -> 使用默认值

### 测试策略

- 验证器测试 18+ 用例（错误测试 >= 30%）
- tail_report 测试 12+ 用例
- 端到端验证测试 10+ 用例
- 诚实声明验证测试 4+ 用例（epistemological_bias、tail_exclusion_statement、data_grade、rows_failed）
- 边界测试：范围宽度 = 0、范围宽度 = DBL_MAX、100000 行 batch

## 提取的实体

- [[tail-report]] — conservative_tail_report（已存在）
- [[data-grade]] — 数据等级（已存在）
- [[physics-first]] — 物理优先认识论（已存在）
- [[exclusion-rate]] — 排除率计算机制，衡量约束对采样空间的裁剪程度（新实体）
- [[honesty-declaration]] — 诚实声明机制，tail_report 中的偏差声明和 data_grade 声明（新实体）
