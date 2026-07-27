# partition-window-engine

PartitionWindowEngine 是 v4 Unit U 交付的分组时间窗口计算引擎，实现基于分组的区间窗口聚合。通过 PARTITION BY 将数据按指定列分组，每个分组内独立计算区间窗口聚合结果。支持多分组结果合并，分组数超过 10000 时返回 kTooManyPartitions 错误。分组列含 NULL 时正确处理，空分组跳过不报错。通过 WindowTypeV2::kPartitionBy 枚举值路由，与 v2 聚合引擎的两阶段执行框架集成。

## 相关文档

- [[source-v4-unit-u-spec]] — Unit U 设计规范
- [[source-v4-unit-u-plan]] — Unit U 实施计划
