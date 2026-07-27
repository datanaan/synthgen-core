# v2 Unit K Spec — 聚合约束引擎

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-k-design.md
> 编译日期：2026-05-14

## 摘要

Unit K 是 v2 两阶段执行的核心——聚合约束引擎实现阶段二的窗口聚合验证。聚合约束检查一组行（时间窗口）的统计特征（AVG/SUM/MIN/MAX/COUNT），而非单行或行间关系。两阶段执行模型：阶段一逐行过滤（值域+行间），阶段二窗口聚合验证。支持 INTERVAL 时间窗口、partial_window_excluded 标记、窗口排除率计算。估算 1.5 周，依赖 v1 值域验证器和 Unit J 行间引擎。

## 关键要点

- 聚合约束语义：检查窗口聚合统计特征，典型如 `AVG(temperature) OVER (INTERVAL 1 HOUR) < 40.0`
- 两阶段执行：PHASE_ONE（值域+行间逐行过滤）→ PHASE_TWO（窗口聚合验证）
- 5 种聚合函数：AVG、SUM、MIN、MAX、COUNT
- 时间窗口类型：v2 仅支持 INTERVAL，v4 扩展 ROWS/SESSION
- 窗口排除率：WindowExclusionRate 包含 partial_window_excluded 标记
- Parser 扩展：识别 OVER/INTERVAL 聚合约束语法
- 测试要求：至少 30 个测试用例，错误测试占比 >= 33%

## 提取的实体

- [[aggregate-engine]] — 聚合约束引擎，两阶段执行核心
- [[two-phase-execution]] — 两阶段执行模型（PHASE_ONE → PHASE_TWO）
- [[constraint-layering]] — 聚合约束属于 PHASE_TWO
- [[inter-row-engine]] — 阶段一消费行间引擎
- [[evidence-package]] — PhaseOneResult/PhaseTwoResult 供证据包构建
