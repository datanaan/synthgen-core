# 帧缓冲区 (FrameBuffer)

> 类型：数据结构
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

帧缓冲区是行间约束引擎用于跨 batch 边界约束检查的数据结构，存储上一 batch 的最后 N 行数据。

## 详情

帧缓冲区基于 deque 实现，核心设计：
- 默认大小：kDefaultSize = 2（存储上一 batch 最后 2 行）
- 操作：push（压入新行）、back（最近一行）、empty、clear
- 容量管理：push 时检查 max_size，超出则 pop_front
- 跨 batch 传递：当前 batch 的最后 N 行写入帧缓冲区，下一 batch 的第一行通过帧缓冲区获取前一批次的尾行

帧缓冲区解决的核心问题是：batch 1 的最后一行与 batch 2 的第一行之间的行间约束需要检查，但它们分属不同 batch。帧缓冲区存储跨 batch 边界的行数据，使行间约束引擎能正确处理跨 batch 的相邻行关系。

## v2 范围

v2 Unit J Task 2 Step 2.1 实现帧缓冲区：
- `src/engine/constraint/inter_row_state.h`：FrameBuffer 结构体定义
- deque 实现，默认 kDefaultSize = 2
- 未来可配置帧缓冲区大小（覆盖更复杂的行间约束）

## 关联实体

- [[inter-row-engine]] — 行间约束引擎使用帧缓冲区
- [[constraint-layering]] — 行间约束是三类约束体系的第二层

## 来源

- [[source-v2-unit-j-spec]] — 二、2.1 核心语义
- [[source-v2-unit-j-plan]] — Task 2 Step 2.1
