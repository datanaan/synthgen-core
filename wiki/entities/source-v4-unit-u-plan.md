# v4 Unit U Plan — 行数窗口 + 分组时间窗口

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-unit-u-plan.md
> 编译日期：2026-05-14

## 摘要

Unit U 实施计划分 5 个 Task，估算 2 周，依赖 v2 #11 聚合约束引擎。Task 1 定义 WindowTypeV2 枚举（kInterval/kRows/kPartitionBy）及 RowsWindowDef 和 PartitionWindowDef 数据结构。Task 2 实现 RowsWindowEngine（ROWS N 最近 N 行聚合、ROWS BETWEEN 范围聚合、排序和溢出处理）。Task 3 实现 PartitionWindowEngine（PARTITION BY 分组 + 区间窗口计算、多分组结果合并、NULL 值处理）。Task 4 与 v2 聚合引擎集成（AggregateConstraintEngine 适配、两阶段执行框架复用）。Task 5 错误处理和测试（10 个 ErrorCode、28+ 测试、v2+v4 混合窗口集成测试）。

## 关键要点

- WindowTypeV2：kInterval（v2 兼容）/ kRows / kPartitionBy 三种类型
- RowsWindowEngine：ROWS N 聚合、ROWS BETWEEN 范围聚合、溢出返回 kWindowOverflow
- PartitionWindowEngine：分组内区间窗口、分组数 >10000 返回 kTooManyPartitions
- 与 v2 聚合引擎集成：两阶段执行（预扫描 + 约束检查）
- 10 个 ErrorCode 覆盖全部错误路径
- 28+ 测试（单元 20+，错误 8+，性能 2），错误测试占比 >= 30%
- 风险：v2 聚合引擎接口不兼容、分组数爆炸、行数窗口排序歧义

## 提取的实体

- [[rows-window-engine]] — 行数窗口计算引擎
- [[partition-window-engine]] — 分组时间窗口计算引擎
