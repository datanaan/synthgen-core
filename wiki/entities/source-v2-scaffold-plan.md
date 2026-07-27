# v2 脚手架 Plan — 暗线增强

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-scaffold-plan.md
> 编译日期：2026-05-14

## 摘要

v2 脚手架实施计划分 5 个 Task，估算 1 周。Task 1 Explain 增强（定义+组件更新+测试），Task 2 Trace 增强（后筛选 span + 行间/聚合 span + 测试），Task 3 可观测性增强（新增 Metrics 注册+测试），Task 4 错误注入 v2（5 个注入场景+测试），Task 5 退化路径回归测试（5 条路径各 1 个回归测试）。

## 关键要点

- Task 1：ExplainInfoV2 扩展，路由器/后筛选/数据引擎的 explain() 更新
- Task 2：后筛选排除率子 span，行间/聚合引擎状态传递 span
- Task 3：注册 router_path_selected、post_filter_exclusion_rate、audit_chain_verification_status 等指标
- Task 4：5 个注入场景全部正确响应
- Task 5：`tests/regression/degradation_path_test.cpp`，5 条路径回归测试
- 产出文件：`src/scaffold/explain.h`（扩展）

## 提取的实体

- [[scaffolding]] — 脚手架暗线实施
- [[degradation-path]] — 退化路径回归
- [[exclusion-rate]] — 排除率 Metrics
