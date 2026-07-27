# v4 Unit W Spec — 完备度连续化评分

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-unit-w-design.md
> 编译日期：2026-05-14

## 摘要

Unit W 是 v4 诚实性的核心，交付完备度连续化评分（#28）——从布尔判断走向 0.0-1.0 连续评分，反映约束系统的真实覆盖能力。CompletenessScorer 基于 5 维加权评分：值域约束(0.3)、行间约束(0.2)、聚合约束(0.2)、统计签名(0.2)、物理合法性(0.1，v1 起默认 1.0)。is_fully_constrained 是 score == 1.0 的特例而非默认。完备度评分影响执行路由器的路径选择。估算 1 周，依赖 v2 #13 执行路由器。

## 关键要点

- 评分公式：completeness_score = Σ(dimension_weight_i x dimension_score_i)，dimension_score_i = constrained_fields_i / total_fields_i
- 5 个维度：值域约束(0.3)、行间约束(0.2)、聚合约束(0.2)、统计签名(0.2)、物理合法性(0.1)
- is_fully_constrained 当且仅当 score == 1.0；should_allow_full_function 仅当 score == 1.0 返回 true
- 3 条诚实声明：布尔判断是 1.0 特例不是默认、高评分不等于高质量数据、物理合法性维度始终为 1.0
- 至少 15 个测试用例，错误测试占比 >= 25%

## 提取的实体

- [[completeness-scoring]] — 完备度连续化评分系统，5 维加权 0.0-1.0 连续评分
