# v2 Unit L Spec — 约束分类器

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-l-design.md
> 编译日期：2026-05-14

## 摘要

Unit L 是 v2 执行路由器的前置组件——约束分类器在编译时确定约束类型和执行模式。将约束分为三类（值域/行间/聚合），标记执行阶段（PHASE_ONE/PHASE_TWO），推导执行模式（kRowByRow/kStatefulBatch/kTwoPhase）。分类规则：聚合优先级最高（有聚合 → kTwoPhase），行间次之（有行间但无聚合 → kStatefulBatch），仅值域 → kRowByRow。估算 1 周，依赖 Parser 扩展。

## 关键要点

- 分类器职责：编译时确定约束组合的执行模式，是路由器做路由决策的基础
- 三种约束类型：kValueRange（值域）、kInterRow（行间）、kAggregate（聚合）
- 两种执行阶段：kPhaseOne（值域+行间）、kPhaseTwo（聚合）
- 三种执行模式：kRowByRow（逐行）、kStatefulBatch（batch 有状态）、kTwoPhase（两阶段）
- ClassificationResult：含分类详情、计数字段、辅助查询方法（phase_one/phase_two 过滤）
- 错误码：7 个，含 kOrderColumnRequired（行间需 ORDER 列）、kDatetimeColumnRequired（聚合需 DATETIME）
- 测试要求：至少 20 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[constraint-classifier]] — 编译时约束分类器
- [[constraint-layering]] — 三类约束体系分类规则
- [[execution-router]] — 消费 ClassificationResult 做路由决策
