# completeness-scorer

CompletenessScorer 是完备度评分的核心计算器，负责将约束系统的覆盖情况转化为 0.0-1.0 连续评分。每个维度的评分公式为 constrained_fields / total_fields，加权汇总公式为 completeness_score = SUM(weight_i * score_i)。提供 should_allow_full_function(score, threshold) 接口用于判断是否允许全功能路径。与执行路由器集成，低完备度走保守路径，高完备度走全功能路径。

## 相关文档

- [[source-v4-unit-w-spec]] — Unit W 设计规范
- [[source-v4-unit-w-plan]] — Unit W 实施计划
- [[source-v4-scaffold-spec]] — 脚手架 Explain 增强
