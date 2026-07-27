# 诚实声明 (Honesty Declaration)

> 类型：机制

## 定义

SynthGen Core 在 tail_report 和 EvidencePackage 中的偏差声明和等级声明机制。核心原则：永远不夸大能力，永远不遗漏已知限制。

## v1 诚实声明项

| 字段 | v1 值 | 含义 |
|------|-------|------|
| epistemological_bias | "physical_first" | 数据生成基于物理约束优先 |
| tail_exclusion_statement | 非空 | 排除行为声明 |
| data_grade | "physics_guaranteed" | 物理引擎保证采样在约束内 |
| audit_immutability | "not_applicable" | v1 无审计不可变性 |
| statistical_fidelity | "not_applicable" | v1 无统计保真度 |
| drift_detection | "not_applicable" | v1 无漂移检测 |
| constraint_type_breakdown | "not_applicable" | v1 无约束类型分解 |

## 验证规则

- 偏差声明缺失 -> `kHonestyViolation`
- data_grade 错误 -> `kHonestyViolation`
- exclusion_rate != 0.0（但 rows_failed = 0）-> `kConsistencyError`
- rows_failed_validation != 0 -> `kConsistencyError`
- 错误标记 not_applicable 字段 -> `kHonestyViolation`

## 设计哲学

- **not_applicable 是设计决策，不是缺失功能**：v1 的能力边界由设计决定
- **永远不在截止日期压力下删除已知限制声明**
- 验证器自动检查诚实声明的完整性

## 关联实体

- [[input-honesty]] — 输入诚实性是诚实声明的前提
- [[tail-report]] — tail_report 包含诚实声明
- [[evidence-package]] — EvidencePackage 包含诚实声明字段
- [[data-grade]] — data_grade 是诚实声明的核心输出
