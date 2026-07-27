# v4 工具线 Plan — Trace 分析 v0.3

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-tool-plan.md
> 编译日期：2026-05-14

## 摘要

v4 工具线实施计划仅含 1 个 Task，估算 0 周（嵌入其他 Unit 测试中）。Task 1 将 Trace 分析工具从 v0.2 升级到 v0.3，新增 4 条规则：counter_example_status_check（反例搜索未成功）、completeness_score_drop（评分显著下降）、search_iteration_exceeded（搜索迭代过多）、model_provenance_missing（模型溯源缺失）。测试产出 tools/trace_analyzer/tests/v0_3_rules_test.py，4+ 测试通过。

## 关键要点

- 仅 1 个 Task，0 周独立估算（嵌入其他 Unit 测试中执行）
- 4 条新增分析规则，不误报，与 v0.2 兼容
- 测试产出：tools/trace_analyzer/tests/v0_3_rules_test.py
- v4 工具线无新增工具，仅维护升级

## 提取的实体

- [[trace-analyzer]] — Trace 分析工具 v0.3
