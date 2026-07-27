# ValueRangeValidator

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

值域约束验证器，作为安全网逐行验证采样结果是否在矩形值域约束内。v1 纯物理路径下应 100% 通过。

## 详情

核心接口：
- `validate_batch(batch)` → ValidationResult（通过/失败行数、失败详情）
- `validate_row(row)` → bool（单行验证，用于调试）
- `explain()` → ExplainInfo

ValidationResult 包含 rows_checked、rows_passed、rows_failed、pass_rate、failures 列表（最多 100 个）。

**设计哲学**：即使物理引擎保证在值域内采样，验证器仍逐行检查。发现越界 → 记录到 tail_report。验证器不删除。

## v1 范围

v1 仅验证矩形值域约束。不支持行间约束、聚合约束验证。

## 关联实体

- [[tail-report]] — 验证结果写入 tail_report
- [[physics-engine]] — 上游采样器
- [[exclusion-rate]] — 排除率指标
- [[data-grade]] — 验证结果影响数据等级

## 来源

- [[source-v1-unit-e-spec]] — Unit E 设计规范
