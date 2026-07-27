# session-window-engine

SessionWindowEngine 是 v4 Unit V 交付的会话窗口计算引擎，实现基于活动间隔（GAP）的自动窗口切分。两次事件间隔超过 GAP 则切分为新会话，窗口大小由数据本身决定而非预定义。支持多分组键独立切分和会话内聚合计算。GAP 边界处理：恰好等于间隔时切分。会话数超过 100000 返回 kTooManySessions 错误。通过 WindowTypeV2::kSession 枚举值路由，与行数窗口、分组窗口通过统一接口调度。

## 相关文档

- [[source-v4-unit-v-spec]] — Unit V 设计规范
- [[source-v4-unit-v-plan]] — Unit V 实施计划
