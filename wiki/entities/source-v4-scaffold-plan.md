# v4 脚手架 Plan — Explain + Trace + 可观测性 + 测试

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-scaffold-plan.md
> 编译日期：2026-05-14

## 摘要

v4 脚手架实施计划分 4 个 Task，估算 0.5 周。Task 1 Explain v4 增强（完备度评分展示 completeness.score + dimensions 分解、模型溯源展示 model_provenance、反例搜索状态展示 counter_example_search.status），Task 2 Trace v4 增强（completeness_score_v4 span 和 counter_example_search_v4 span），Task 3 可观测性（completeness_score 和 counter_example_status 两个 Prometheus 指标），Task 4 测试（产出 tests/unit/v4_scaffold_test.cpp，8+ 测试通过）。deferred 状态下 Explain 不报错、Trace 不记录反例搜索 span。

## 关键要点

- Task 1：修改 src/explain/explain_engine.h/cpp，新增完备度/模型溯源/反例搜索字段
- Task 2：修改 src/trace/，新增 completeness_score_v4 和 counter_example_search_v4 span
- Task 3：修改 src/metrics/，暴露 completeness_score(gauge) 和 counter_example_status(0/1/2)
- Task 4：tests/unit/v4_scaffold_test.cpp，8+ 测试
- 每个 Task 0.125 周

## 提取的实体

- [[scaffolding]] — 脚手架暗线实施
- [[constraint-completeness-scoring]] — 完备度评分集成
