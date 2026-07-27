# v2 Unit P Spec — DURING/WHEN + EvidencePackage v2

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-p-design.md
> 编译日期：2026-05-14

## 摘要

Unit P 包含两个组件：#16 DURING/WHEN 语义（1 周）实现条件约束，#17 EvidencePackage v2（0.5 周）扩展证据包字段。DURING 语义：当指定列等于特定值时约束生效（如 `DURING status = "normal" THEN temperature BETWEEN -10 AND 45`）。WHEN 语义：当条件为真时约束生效。条件约束产生非矩形约束域，需拒绝采样/MCMC 处理。EvidencePackage v2 新增 statistical_fidelity、constraint_type_breakdown、generator_identity、audit_immutability、post_filter_info 字段。

## 关键要点

- DURING 语义：列值等值条件触发约束，`DURING column = value THEN constraint`
- WHEN 语义：布尔条件触发约束，`WHEN condition THEN constraint`
- 非矩形约束域：条件约束域不是简单矩形，需拒绝采样（拒绝率>90% 时 MCMC）
- EvidencePackage v2 新增 5 个字段：statistical_fidelity、constraint_type_breakdown、generator_identity、audit_immutability("verified")、post_filter_info
- v1→v2 迁移：schema_version 升级 "v1"→"v2"，v1 字段保持兼容
- 测试要求：#16 至少 15 个、#17 至少 10 个

## 提取的实体

- [[during-when-semantics]] — DURING/WHEN 条件约束语义
- [[conditional-constraint]] — 条件约束引擎
- [[evidence-package]] — EvidencePackage v2 字段扩展
- [[identity-switch]] — generator_identity 身份声明
