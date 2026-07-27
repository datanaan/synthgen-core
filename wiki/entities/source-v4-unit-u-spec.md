# v4 Unit U Spec — 行数窗口 + 分组时间窗口

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-unit-u-design.md
> 编译日期：2026-05-14

## 摘要

Unit U 是 v4 窗口扩展的基础，交付行数窗口（#25 ROWS）和分组时间窗口（#26 PARTITION BY + INTERVAL）。行数窗口基于物理行号而非时间区间（OVER ROWS 100 / OVER ROWS BETWEEN 50 PRECEDING AND CURRENT ROW），与 v2 区间窗口形成互补。分组时间窗口按分组键拆分后在每组内应用区间窗口（OVER PARTITION BY region, INTERVAL 1 HOUR），复用 v2 #11 AggregateConstraintEngine 的两阶段执行框架。估算 2 周，依赖 v2 #11 聚合约束引擎。

## 关键要点

- RowsWindowEngine：基于行数的滑动/翻滚窗口，ROWS N 最近 N 行、ROWS BETWEEN M PRECEDING AND N FOLLOWING 标准范围
- PartitionWindowEngine：按列值分组后每组内应用区间窗口，分组列必须存在于 Schema
- WindowTypeV2 枚举扩展：新增 kRows 和 kPartitionBy，与 kInterval 统一调度
- 行数窗口 6 个错误码（kInvalidRowCount, kUndefinedColumn, kEmptyBatch 等），分组窗口 6 个错误码（kUndefinedPartitionColumn, kEmptyPartition 等）
- 至少 20 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[rows-window]] — 行数窗口引擎，基于物理行号的滑动/翻滚窗口聚合
- [[partition-window]] — 分组时间窗口引擎，按分组键拆分后应用区间窗口
- [[window-type-v2]] — v4 扩展的窗口类型枚举（kInterval/kRows/kPartitionBy/kSession）
