# 聚合约束引擎 (AggregateEngine)

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

聚合约束引擎是 SynthGen Core 两阶段执行的核心组件，实现阶段二的窗口聚合验证。聚合约束检查一组行（时间窗口）的统计特征（AVG/SUM/MIN/MAX/COUNT），而非单行值域或行间关系。

## 详情

聚合约束引擎实现两阶段执行模型：

- **阶段一（PHASE_ONE）**：逐行过滤，调用值域验证器 + 行间引擎
- **阶段二（PHASE_TWO）**：在阶段一输出上按时间窗口分组，计算聚合值，验证是否满足约束

核心数据结构：
- `AggregateConstraintDef`：聚合约束定义（列名、函数、窗口类型/规格、范围）
- `AggregationWindow`：时间窗口（行索引范围、包含/排除行、partial 标记）
- `WindowExclusionRate`：窗口排除率 + partial_window_excluded 标记
- `TwoPhaseResult`：两阶段执行结果（含整体排除率）

支持的聚合函数：AVG、SUM、MIN、MAX、COUNT。v2 仅支持 INTERVAL 时间窗口，v4 扩展 ROWS/SESSION 窗口。

语法示例：`AVG(temperature) OVER (INTERVAL 1 HOUR) < 40.0`

## v2 范围

v2 Unit K 完整实现聚合约束引擎，包括：
- AggregateEngine 两阶段执行
- 时间窗口（INTERVAL）划分和 partial 窗口标记
- 5 种聚合函数计算
- 窗口排除率计算
- Parser OVER/INTERVAL 语法扩展
- Trace/Explain/Metrics 脚手架集成
- 至少 30 个测试用例（错误测试占比 >= 33%）

## 关联实体

- [[two-phase-execution]] — 两阶段执行模型
- [[constraint-layering]] — 聚合约束属于 PHASE_TWO
- [[inter-row-engine]] — 阶段一消费行间引擎
- [[execution-router]] — 通过路由器间接使用
- [[evidence-package]] — PhaseOneResult/PhaseTwoResult 供证据包构建
- [[scaffolding]] — 两阶段分别产生子 span

## 来源

- [[source-v2-unit-k-spec]] — 二、#11 聚合约束引擎
- [[source-v2-unit-k-plan]] — Task 4：AggregateEngine 核心实现
