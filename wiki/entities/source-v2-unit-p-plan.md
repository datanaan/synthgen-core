# v2 Unit P Plan — DURING/WHEN + EvidencePackage v2

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-p-plan.md
> 编译日期：2026-05-14

## 摘要

Unit P 实施计划分 Part A（DURING/WHEN，6 步骤，1 周）和 Part B（EvidencePackage v2，4 步骤，0.5 周），共 10 步骤，总计 1.5 周。Part A：Parser 条件约束语法扩展、条件约束引擎实现（apply + 非矩形域生成）、引擎测试。Part B：EvidencePackageV2 字段扩展、构建器扩展、Schema 验证、10+ 测试。

## 关键要点

- Part A Task A1：DURING/WHEN/THEN Token 和 AST 扩展，条件约束语法解析
- Part A Task A2：ConditionalConstraintEngine 实现，apply() 方法 + 拒绝采样/MCMC
- Part B Task B1：EvidencePackageV2 结构体扩展，构建器支持 v2 字段填充
- 产出文件：`src/engine/constraint/conditional_engine.h/.cpp`、`src/engine/evidence/evidence_package.h`（扩展）
- 风险：高维条件空间拒绝采样效率低，v2 仅支持简单等值和比较条件

## 提取的实体

- [[during-when-semantics]] — DURING/WHEN 语法和引擎
- [[conditional-constraint]] — 条件约束引擎实现
- [[evidence-package]] — v2 字段扩展
