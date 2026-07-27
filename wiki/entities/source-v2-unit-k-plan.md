# v2 Unit K Plan — 聚合约束引擎

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-k-plan.md
> 编译日期：2026-05-14

## 摘要

Unit K 实施计划分 7 个 Task、17 个步骤，估算 1.5 周。Task 1 扩展 Parser（聚合 Token、AST、解析），Task 2 实现时间窗口划分，Task 3 实现 5 种聚合函数，Task 4 实现 AggregateEngine 两阶段核心，Task 5 错误处理和边界条件，Task 6 脚手架集成（Trace/Explain/Metrics），Task 7 端到端集成测试。

## 关键要点

- Task 1 Parser 扩展：新增 K_AVG/K_SUM/K_MIN/K_MAX/K_COUNT、K_OVER/K_INTERVAL 等 Token
- Task 2 时间窗口：解析 INTERVAL 规格为秒数，按 ORDER 列划分连续窗口
- Task 3 聚合函数：AggregateFunctionExecutor 静态方法实现 5 种聚合
- Task 4 核心：阶段一调用 ValueRangeValidator + InterRowEngine，阶段二窗口聚合验证
- Task 6 脚手架：两阶段分别产生子 span，metrics 暴露阶段耗时/窗口数/排除率
- 产出文件：`src/engine/constraint/aggregate_engine.h/.cpp`、`src/engine/constraint/window_spec.h/.cpp`

## 提取的实体

- [[aggregate-engine]] — AggregateEngine 两阶段执行实现
- [[two-phase-execution]] — 两阶段执行模型
- [[inter-row-engine]] — 阶段一消费行间引擎
- [[scaffolding]] — 脚手架集成
