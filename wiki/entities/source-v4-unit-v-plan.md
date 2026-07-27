# v4 Unit V Plan — 会话窗口

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-unit-v-plan.md
> 编译日期：2026-05-14

## 摘要

Unit V 实施计划分 5 个 Task，估算 1.5 周，依赖 Unit U（#25+#26）。Task 1 定义 SessionWindowDef 数据结构（session_column/gap_ms/aggregate_column 等）和 WindowTypeV2 新增 kSession 枚举值。Task 2 实现 SessionWindowEngine 核心（按 GAP 切分会话、会话内聚合计算、多分组键支持）。Task 3 边界和异常处理（GAP=1ms/1年/恰好=间隔、NULL 值、会话数 >100000 阈值）。Task 4 与窗口系统统一集成（kSession 路由适配、三种窗口类型统一接口调度）。Task 5 测试（7 个 ErrorCode、22+ 测试、三种窗口类型集成验证）。

## 关键要点

- SessionWindowDef：gap_ms=0/-1 返回 kInvalidGapMs
- 会话切分算法：同一会话内事件间隔 < GAP，恰好=间隔时切分
- WindowTypeV2 扩展：新增 kSession，与 kInterval/kRows/kPartitionBy 兼容
- 多分组键独立切分，会话数 >100000 返回 kTooManySessions
- 22+ 测试（单元 16+，错误 6+，GAP 精确 3），错误测试占比 >= 30%
- 风险：会话切分内存消耗、GAP 边界语义歧义、与 Unit U 接口变更

## 提取的实体

- [[session-window-engine]] — 会话窗口计算引擎
