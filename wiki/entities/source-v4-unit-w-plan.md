# v4 Unit W Plan — 完备度连续化评分

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-unit-w-plan.md
> 编译日期：2026-05-14

## 摘要

Unit W 实施计划分 5 个 Task，估算 1 周，依赖 v2 #13 执行路由器。Task 1 定义 DimensionScore（5 个维度 + 权重）和 CompletenessScore（score + dimension_scores + is_fully_constrained）数据结构。Task 2 实现 CompletenessScorer 核心（维度评分 = constrained_fields / total_fields、加权汇总 completeness_score = SUM(weight_i * score_i)、should_allow_full_function 阈值判断）。Task 3 与执行路由器集成（路由器根据完备度调整路径选择、完备度影响身份声明）。Task 4 脚手架集成（Explain 增强、Trace 集成、Prometheus 指标）。Task 5 测试（5 个 ErrorCode、20+ 测试、完备度路由集成测试）。

## 关键要点

- 5 个维度：value_range(0.3) / inter_row(0.2) / aggregate(0.2) / statistical_signature(0.2) / physical_legality(0.1)
- 维度评分 = constrained_fields / total_fields
- is_fully_constrained 当且仅当 score == 1.0
- should_allow_full_function：score >= threshold 时返回 true
- 低完备度（<0.6）走保守路径，高完备度（>=1.0）走全功能路径
- 5 个 ErrorCode：kInvalidDimensionWeight、kScoreOutOfBounds、kMissingOrderColumn、kDimensionConflict、kSerializationError
- 20+ 测试，错误测试占比 >= 30%
- 风险：维度权重争议、权重和不等于 1.0、评分精度不足

## 提取的实体

- [[constraint-completeness-scoring]] — 完备度连续化评分系统
- [[completeness-scorer]] — 完备度评分计算器
