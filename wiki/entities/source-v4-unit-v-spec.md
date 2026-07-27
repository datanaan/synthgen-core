# v4 Unit V Spec — 会话窗口

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-unit-v-design.md
> 编译日期：2026-05-14

## 摘要

Unit V 是 v4 窗口类型扩展的完成，交付会话窗口（#27 SESSION）——基于活动间隔自动切分，无需预定义窗口边界。会话窗口按 GAP 阈值自动识别会话边界：两次事件间隔超过 GAP 则切分为新会话（OVER SESSION user_id, GAP 30 MINUTES）。窗口大小由数据本身决定，适合用户行为分析、交易流水分析等场景。估算 1.5 周，依赖 Unit U (#25+#26)。通过 WindowTypeV2::kSession 统一调度。

## 关键要点

- SessionWindowEngine：基于 GAP 的会话窗口切分和聚合，切分算法按 session_column 排序分组→按时间排序→相邻间隔 > GAP 切分
- 会话窗口特点：数据驱动划分、可变窗口大小、不可能有空窗口
- 与区间/行数窗口对比：划分依据（数据驱动 vs 固定时间/行数）、窗口大小（可变 vs 固定）、窗口边界（数据驱动 vs 预定义）
- 7 个错误码：kInvalidGapMs, kUndefinedSessionColumn, kEmptyBatch, kNonTimestampSessionColumn 等
- 至少 18 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[session-window]] — 会话窗口引擎，基于 GAP 阈值自动切分会话边界
