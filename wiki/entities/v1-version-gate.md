# v1 版本门控 (v1 Version Gate)

> 类型：机制

## 定义

SynthGen Core v1 中对超出当前版本能力的语法和功能的系统性拒绝机制。当用户使用 v1 不支持的语法时，系统返回 `kUnsupportedInV1` 错误码并附带版本提示，而非静默忽略或产生错误结果。

## 设计原则

- **诚实优于能力**：明确告知用户当前版本不支持的功能，不缩小也不夸大能力边界
- **版本提示**：错误消息指明该功能在哪个版本开始支持
- **提前识别**：在 Parser 层就拦截 v1 不支持的语法，不传递到下游

## v1 不支持的语法

| 语法 | 错误码 | 提示消息 |
|------|--------|---------|
| DURING 子句 | kUnsupportedInV1 | "DURING constraints are not supported in v1. Supported from v2." |
| WHEN 子句 | kUnsupportedInV1 | 类似 |
| 行间约束 [t] | kUnsupportedInV1 | "Inter-row constraints are not supported in v1. Supported from v2." |
| 聚合约束 AVG/OVER | kUnsupportedInV1 | "Aggregate constraints are not supported in v1. Supported from v2." |

## v1 不支持的能力（设计层面）

- audit_immutability 字段 -> "not_applicable"
- statistical_fidelity 字段 -> "not_applicable"
- drift_detection 字段 -> "not_applicable"
- constraint_type_breakdown 字段 -> "not_applicable"
- tail_report 的 not_applicable 标记是设计决策，不是缺失功能

## 关联实体

- [[synthlang]] — Parser 层实现版本门控
- [[input-honesty]] — 版本门控是输入诚实性的体现
- [[evidence-package]] — EvidencePackage 的 not_applicable 标记
