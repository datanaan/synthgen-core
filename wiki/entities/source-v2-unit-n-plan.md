# v2 Unit N Plan — 后筛选完整版

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-n-plan.md
> 编译日期：2026-05-14

## 摘要

Unit N 实施计划分 4 个 Task、9 个步骤，估算 1 周。Task 1 实现误差界联动表（ExclusionRateBand 分级和映射），Task 2 实现后筛选核心逻辑（逐行约束检查、排除率预估、超时截断、实时监控），Task 3 错误处理和边界条件，Task 4 端到端集成测试。

## 关键要点

- Task 1：ExclusionRateBand 4 级分级 + data_grade 联动映射
- Task 2 核心：逐行检查值域+行间+聚合约束，排除率预估基于 DataEngineV1::volume_ratio()
- 超时截断：每处理 1000 行检查超时，截断返回部分数据
- 实时监控：每 N 行记录当前排除率
- 产出文件：`src/engine/postfilter/exclusion_band.h/.cpp`、`src/engine/postfilter/post_filter.h/.cpp`

## 提取的实体

- [[post-filter]] — 后筛选核心实现
- [[exclusion-rate]] — 排除率预估与分级
- [[data-grade]] — 排除率联动映射
- [[data-engine]] — volume_ratio() 排除率预估
