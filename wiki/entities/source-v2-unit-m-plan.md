# v2 Unit M Plan — 执行路由器重构

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-m-plan.md
> 编译日期：2026-05-14

## 摘要

Unit M 实施计划分 5 个 Task、17 个步骤，估算 2 周。Task 1 定义退化路径和身份声明，Task 2 实现路由决策算法（体积比计算、排除率预估、路由逻辑），Task 3 v1 接口迁移（含 [COORDINATE] 待决策），Task 4 实现 5 条路径的执行逻辑，Task 5 脚手架集成（Explain/Trace/Metrics）。

## 关键要点

- Task 1：DegradationPath 枚举 + identity_for_path() 映射
- Task 2：体积比计算（数据引擎不可用时保守估计 1.0）、排除率预估、路由决策 15+ 测试
- Task 3 [COORDINATE]：选项 A（v1 入口委托路由器）vs 选项 B（GenerationService 适配器）
- Task 4：5 条路径各实现执行逻辑，纯物理路径与 v1 等价
- Task 5：路由器 Explain 返回所有可用路径+选择理由，metrics 暴露路径选择计数
- 风险：v1 迁移改动量大，数据引擎延迟用 mock 测试

## 提取的实体

- [[degradation-path]] — 退化路径枚举和实现
- [[execution-router]] — 路由器核心实现
- [[exclusion-rate]] — 排除率预估逻辑
- [[identity-switch]] — 身份声明
