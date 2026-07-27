# rows-window-engine

RowsWindowEngine 是 v4 Unit U 交付的行数窗口计算引擎，实现基于物理行号的滑动/翻滚窗口聚合。支持 ROWS N（最近 N 行聚合）和 ROWS BETWEEN（范围聚合）两种语法。与 v2 区间窗口不同，行数窗口按物理行号而非时间区间划分。排序列由用户指定或使用默认顺序，行数超过 batch_size 时返回 kWindowOverflow 错误。通过 WindowTypeV2::kRows 枚举值路由，与 v2 聚合引擎的两阶段执行框架集成。

## 相关文档

- [[source-v4-unit-u-spec]] — Unit U 设计规范
- [[source-v4-unit-u-plan]] — Unit U 实施计划
