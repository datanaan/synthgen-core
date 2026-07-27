# 两阶段执行模型

> 类型：概念
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

两阶段执行模型是 SynthGen Core 处理混合约束（值域+行间+聚合）的执行策略：阶段一逐行过滤（值域+行间约束），阶段二窗口聚合验证（聚合约束）。

## 详情

两阶段执行的必要性：聚合约束检查的是一组行的统计特征，无法在逐行处理时判定。因此系统先在阶段一完成逐行过滤（排除不满足值域和行间约束的行），再在阶段二对过滤后的数据按时间窗口分组验证聚合约束。

**阶段一（PHASE_ONE）**：
- 调用 ValueRangeValidator（值域约束）
- 调用 InterRowEngine（行间约束）
- 输出：逐行过滤后的数据（PhaseOneResult）

**阶段二（PHASE_TWO）**：
- 在阶段一输出上按时间窗口（INTERVAL）划分
- 计算每个窗口的聚合值（AVG/SUM/MIN/MAX/COUNT）
- 验证聚合值是否满足约束
- 标记不满足约束的窗口（partial_window_excluded）
- 输出：窗口聚合验证结果（PhaseTwoResult）

执行模式推导（由约束分类器完成）：
- 有聚合约束 → kTwoPhase（两阶段）
- 有行间但无聚合 → kStatefulBatch（batch 有状态）
- 仅值域 → kRowByRow（逐行）

## v2 范围

v2 Unit K 完整实现两阶段执行模型：
- AggregateEngine::execute() 整合两阶段
- 阶段一调用值域验证器 + 行间引擎
- 阶段二时间窗口划分 + 聚合验证
- 两阶段分别产生 Trace span
- 整体排除率 = 结合阶段一和阶段二的排除效果

## 关联实体

- [[aggregate-engine]] — 两阶段执行的核心实现
- [[constraint-classifier]] — 推导执行模式
- [[inter-row-engine]] — 阶段一的行间约束处理
- [[evidence-package]] — PhaseOneResult/PhaseTwoResult 写入证据包

## 来源

- [[source-v2-unit-k-spec]] — 二、2.1 核心语义
- [[source-v2-unit-k-plan]] — Task 4：两阶段整合
