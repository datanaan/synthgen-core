# v4 工具线 Spec — Trace 分析 v0.3

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-tool-design.md
> 编译日期：2026-05-14

## 摘要

v4 工具线设计规范无新增工具，仅将 Trace 分析从 v0.2 升级到 v0.3，新增 4 条反例搜索和完备度评分的分析规则：counter_example_status_check（搜索未成功标注）、completeness_score_drop（评分从 >0.8 降到 <0.5 标注显著下降）、search_iteration_exceeded（迭代超过 500 标注过多）、model_provenance_missing（模型溯源缺失标注）。诚实边界：能做基于规则扫描 Trace span 标注异常，做不了理解约束语义判断反例搜索是否合理。

## 关键要点

- 无新增工具，仅 Trace 分析 v0.2 → v0.3 升级
- 4 条新增规则：反例搜索状态检查、完备度评分下降、搜索迭代过多、模型溯源缺失
- 估算 0 周（工具维护，嵌入其他 Unit 测试中）
- 测试验收：至少 4 个测试用例（每规则一个），与 v0.2 已有规则兼容
- 不误报：正常 span 不触发异常标注

## 提取的实体

- [[trace-analyzer]] — Trace 分析工具 v0.3
