# 退化路径 (DegradationPath)

> 类型：概念
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

退化路径是执行路由器根据约束完备性和数据引擎可用性选择的生成策略，从全功能约束驱动到简单格式化扰动的 5 级退化梯度。

## 详情

5 条退化路径按约束完备性从高到低排列：

| 路径 | 身份名 | 条件 | 说明 |
|------|--------|------|------|
| kFullFunction | constraint_driven_synthetic | 约束完备 + 数据引擎可用 | 全功能约束驱动生成 |
| kPostFilter | post_filter_synthetic | 排除率 < 90% | 物理采样 + 后筛选过滤 |
| kPurePhysics | physics_sampler | 仅值域约束 / 无数据引擎 | v1 等价路径 |
| kStatisticalGeneration | statistical_generator | 约束不完备 + 数据引擎可用 | 统计生成 |
| kKDEPerturbation | kde_perturbation_generator | 约束极度不完备 + 数据引擎可用 | 格式化扰动 |

路由决策算法流程：
1. 检查数据引擎可用性
2. 计算体积比（数据引擎可用时）
3. 预估排除率
4. 从高到低匹配路径
5. 构建身份声明

关键参数：排除率 >90% 拒绝后筛选，体积比保守估计默认 1.0。

## v2 范围

v2 Unit M 定义并实现 5 条退化路径完整体系：
- DegradationPath 枚举和 identity_for_path() 映射
- RoutingDecision 路由决策结构（含身份声明、体积比、排除率预估）
- ExecutionRouter::route() 路由决策算法
- 5 条路径各自的执行逻辑
- v1 接口迁移（v1 硬编码 → 路由器驱动）
- 退化路径回归测试（5 条路径各 1 个端到端测试）

## 关联实体

- [[execution-router]] — 路由决策核心组件
- [[identity-switch]] — 身份切换与服务分级
- [[exclusion-rate]] — 排除率预估决定路径选择
- [[data-engine]] — 数据引擎可用性影响路径选择
- [[post-filter]] — kPostFilter 路径的核心实现
- [[scaffolding]] — 退化路径命中率和回归测试

## 来源

- [[source-v2-unit-m-spec]] — 二、#13 执行路由器重构
- [[source-v2-unit-m-plan]] — Task 1 + Task 4
- [[source-v2-scaffold-spec]] — 六、测试增强
