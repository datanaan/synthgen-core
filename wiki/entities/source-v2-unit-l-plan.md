# v2 Unit L Plan — 约束分类器

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-l-plan.md
> 编译日期：2026-05-14

## 摘要

Unit L 实施计划分 5 个 Task、9 个步骤，估算 1 周。Task 1 定义枚举和分类规则，Task 2 实现 ClassificationResult 和辅助方法，Task 3 错误处理，Task 4 功能测试（14+ 用例），Task 5 脚手架集成（Explain + Trace span）。

## 关键要点

- Task 1：定义 ConstraintType、ExecutionPhase、ExecutionMode 枚举和分类规则
- Task 2：ClassificationResult 含 phase_one_constraints()/phase_two_constraints() 辅助方法
- 分类优先级：聚合 > 行间 > 值域
- 预留 v4 窗口类型扩展（kRows/kSession）
- 产出文件：`src/engine/router/constraint_classifier.h/.cpp`

## 提取的实体

- [[constraint-classifier]] — 约束分类器实现
- [[execution-router]] — 消费分类结果
- [[scaffolding]] — Explain/Trace 集成
