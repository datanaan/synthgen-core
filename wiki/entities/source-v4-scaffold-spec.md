# v4 脚手架 Spec — Explain 增强 + Trace 增强

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-scaffold-design.md
> 编译日期：2026-05-14

## 摘要

v4 脚手架设计规范定义两项增强，估算 0.5 周。Explain 增强包括完备度评分展示（score + 5 维度分解 + 权重 + is_fully_constrained + path_decision）、模型溯源展示（model_name/version_id/fidelity_score/was_compacted）、反例搜索状态展示（status + violation_regions_count）。Trace 增强包括完备度评分变化 Trace span（completeness_score_v4）和反例搜索轨迹 Trace span（counter_example_search_v4，含 iterations/duration_ms/violation_regions_count）。可观测性要求 completeness_score 和 counter_example_status 作为 Prometheus gauge 暴露。

## 关键要点

- Explain 完备度评分：5 维度（value_range/inter_row/aggregate/statistical_signature/physical_legality）
- Explain 模型溯源：从 ModelVersionChain 填充 ModelVersionProvenance
- Explain 反例搜索状态：三态（available/deferred/未执行）
- Trace completeness_score_v4 span：评分、是否全约束、维度数量
- Trace counter_example_search_v4 span：搜索状态、迭代次数、耗时、违反区域数
- deferred 状态下 Explain 不报错，Trace 不记录反例搜索 span
- 测试验收：至少 8 个测试用例

## 提取的实体

- [[scaffolding]] — 脚手架暗线设计
- [[constraint-completeness-scoring]] — 完备度评分 Explain 展示
