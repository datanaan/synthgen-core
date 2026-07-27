# 约束分类器

> 类型：组件
> 首次编译：2026-05-11

## 定义

编译阶段的组件，识别约束类型并标记执行阶段（PHASE_ONE / PHASE_TWO），决定执行模式（row_by_row / stateful_batch / two_phase）。

## 详情

**分类逻辑**：
1. 识别所有值域约束 → PHASE_ONE（最高优先级）
2. 识别所有行间约束 → PHASE_ONE（有状态执行）
3. 识别所有聚合约束 → PHASE_TWO

**执行模式决策**：
- 仅值域 → row_by_row
- 含行间 → stateful_batch
- 含聚合 → two_phase（阶段一执行值域+行间，阶段二执行聚合）

**版本对应**：
- v1：Parser 内隐含分类（仅值域约束），无独立分类器
- v2 #12：独立约束分类器组件，Parser 扩展支持三类约束语法

## 关联实体

- [[constraint-layering]] — 三类约束理论定义
- [[execution-router]] — 分类结果驱动路由

## 来源

- [[source-engineering-framework]] — §3.3 编译阶段约束分类
- [[source-roadmap]] — v2 #12
