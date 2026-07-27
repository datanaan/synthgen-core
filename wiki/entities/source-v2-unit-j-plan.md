# v2 Unit J Plan — 行间约束引擎

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-j-plan.md
> 编译日期：2026-05-14

## 摘要

Unit J 实施计划分 5 个 Task、14 个步骤，估算 1.5 周。Task 1 扩展 Parser（Token、AST、解析、测试），Task 2 实现 InterRowEngine 核心（状态传递、ORDER 列排序、逐行检查、批量接口），Task 3 处理错误和边界条件，Task 4 集成脚手架（Trace span、Explain、Metrics），Task 5 端到端集成测试。

## 关键要点

- Task 1 Parser 扩展：新增 S_LBRACKET、S_RBRACKET、K_ABS 三个 Token
- Task 2 核心实现：InterRowState + FrameBuffer 状态传递，ORDER 列排序，逐行行间检查
- Task 3 错误处理：7 个 ErrorCode 全部实现，9 个边界条件测试
- Task 4 脚手架：RAII SpanGuard 产生 Trace span，explain() 返回约束详情
- Task 5 集成测试：跨 batch 状态传递、混合约束、5 条退化路径中的行间引擎参与
- 产出文件：`src/engine/constraint/inter_row_engine.h/.cpp`、`src/engine/constraint/inter_row_state.h/.cpp`

## 提取的实体

- [[inter-row-engine]] — 行间约束引擎核心实现
- [[frame-buffer]] — FrameBuffer deque 实现，默认 kDefaultSize = 2
- [[scaffolding]] — 脚手架集成：Trace/Explain/Metrics
- [[synthlang-parser]] — Parser 行间约束语法扩展
