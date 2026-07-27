# v2 Unit J Spec — 行间约束引擎

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-j-design.md
> 编译日期：2026-05-14

## 摘要

Unit J 是 v2 的第一个新组件，实现三类约束体系中"行间"层的核心。行间约束检查相邻行之间的关系（变化率、单调性、状态跳变），而非单行值域。核心设计包括：batch 有状态执行（跨 batch 传递状态）、Frame Buffer（存储上一 batch 最后 N 行）、ORDER 列绑定（排序列来自 Schema）。同时扩展 Parser 以识别 `[t]`/`[t-1]` 行间约束语法。估算 1.5 周，依赖 v1 物理引擎和值域验证器。

## 关键要点

- 行间约束语义：检查相邻行关系（delta_max、delta_min、单调递增/递减）
- batch 有状态设计：InterRowState 跨 batch 传递最后一行值，FrameBuffer 存储最后 N 行
- ORDER 列绑定：排序列来自 Schema ORDER 声明，非用户在约束中指定
- 两阶段归属：行间约束属于 PHASE_ONE（与值域约束同阶段）
- Parser 扩展：新增 `[t]`/`[t-1]` 语法识别，支持 ABS() 函数
- 错误码覆盖：kOrderColumnRequired、kUndefinedColumn、kTypeMismatch、kInvalidDelta 等 8 个
- 测试要求：至少 25 个测试用例，错误测试占比 >= 36%

## 提取的实体

- [[inter-row-engine]] — 行间约束引擎，batch 有状态执行，跨 batch 状态传递
- [[frame-buffer]] — 帧缓冲区，存储上一 batch 最后 N 行用于跨 batch 约束检查
- [[constraint-layering]] — 三类约束体系（值域/行间/聚合），行间属于 PHASE_ONE
- [[execution-router]] — 消费行间引擎的路由决策
- [[post-filter]] — 后筛选路径消费行间引擎的过滤率
