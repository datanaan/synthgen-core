# v2 Unit M Spec — 执行路由器重构

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-m-design.md
> 编译日期：2026-05-14

## 摘要

Unit M 是 v2 的核心重构——将 v1 硬编码调度逻辑重构为多路径路由器驱动。定义 5 条退化路径（按约束完备性从高到低）：kFullFunction（全功能约束驱动）、kPostFilter（后筛选）、kPurePhysics（v1 等价）、kStatisticalGeneration（统计生成）、kKDEPerturbation（KDE 扰动）。每条路径有对应身份声明。引入体积比预估、排除率预估用于路由决策。含 [COORDINATE] v1 接口兼容策略待决策（选项 A 完全重构 vs 选项 B 适配器模式）。估算 2 周。

## 关键要点

- 5 条退化路径：按约束完备性和数据引擎可用性从高到低路由
- 身份声明：每条路径有唯一身份名（如 constraint_driven_synthetic、post_filter_synthetic）
- 体积比：constraint_volume / data_distribution_volume，数据引擎可用时计算
- 排除率预估：基于体积比，>90% 拒绝后筛选
- 路由决策算法：完备+数据引擎→全功能；排除率<90%→后筛选；仅值域→纯物理
- v1 接口迁移：推荐选项 A（完全重构），v1 入口委托路由器
- 测试要求：至少 30 个测试用例

## 提取的实体

- [[degradation-path]] — 5 条退化路径枚举和身份声明
- [[execution-router]] — 执行路由器重构
- [[exclusion-rate]] — 排除率预估与分级
- [[identity-switch]] — 身份切换机制
- [[data-engine]] — 体积比计算依赖
