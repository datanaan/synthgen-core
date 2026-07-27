# 行间约束引擎 (InterRowEngine)

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

行间约束引擎是 SynthGen Core 三类约束体系中"行间"层的核心组件，检查相邻行之间的关系（变化率、单调性、状态跳变），而非单行内部的值域。

## 详情

行间约束引擎采用 batch 有状态执行模型，数据按 batch 处理，行间约束跨 batch 传递状态（上一 batch 最后一行的值）。核心设计包括：

- **排序列绑定**：行间约束依赖行的顺序，顺序由 Schema 的 ORDER 列定义
- **Frame Buffer**：存储上一 batch 最后 N 行，用于跨 batch 边界处的约束检查
- **约束类型**：kDeltaMax（变化率上限）、kDeltaMin（变化率下限）、kMonotoneIncrease（单调递增）、kMonotoneDecrease（单调递减）
- **执行阶段**：行间约束属于 PHASE_ONE，与值域约束同阶段执行

语法示例：`vibration[t] - vibration[t-1] < 5.0`（变化率约束）

接口核心：`execute_batch(batch, incoming_states) → InterRowResult`，输出包含 filtered_batch、outgoing_states、rows_passed/rows_filtered/filter_rate 统计。

## v2 范围

v2 Unit J 完整实现行间约束引擎，包括：
- InterRowEngine 核心类和 execute_batch 接口
- InterRowState 跨 batch 状态传递
- FrameBuffer 帧缓冲区（默认大小 2）
- ORDER 列排序和 NULL 处理
- Parser [t]/[t-1] 语法扩展
- Trace/Explain/Metrics 脚手架集成
- 至少 25 个测试用例（错误测试占比 >= 36%）

## 关联实体

- [[constraint-layering]] — 行间约束是三类约束体系的第二层
- [[frame-buffer]] — 跨 batch 帧缓冲区
- [[execution-router]] — 消费行间引擎做路由决策
- [[aggregate-engine]] — 阶段一消费行间引擎
- [[synthlang-parser]] — Parser 行间约束语法扩展
- [[scaffolding]] — Trace span、Explain、Metrics 集成

## 来源

- [[source-v2-unit-j-spec]] — 二、#10 行间约束引擎
- [[source-v2-unit-j-plan]] — Task 2：InterRowEngine 核心实现
