# v3 脚手架 Spec — 5 项增强

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-scaffold-design.md
> 编译日期：2026-05-14

## 摘要

v3 脚手架交付 5 项增强，与 Unit Q-T 并行开发，估算 1 周。增强内容包括：Explain 增强（compaction 影响预估，显示退化版本和偏差报告）、Trace 增强（持续对齐模型更新前后变化，span 含 drift_score 和 compensation_status）、可观测性增强（版本链状态 + GC 历史，新增 model_versions_count 和 gc_compaction_history 指标）、错误注入增强（compaction 冲突场景注入）、测试增强（compaction 前后一致性验证）。

## 关键要点

- Explain 增强：explain() 显示退化版本和偏差报告，compaction 影响预估
- Trace 增强：模型更新 span 含 drift_score + compensation_status
- 可观测性增强：/metrics 新增 model_versions_count + gc_compaction_history
- 错误注入增强：注入 compaction 冲突后系统正确处理
- 测试增强：compaction 后生成结果与直接生成结果一致

## 提取的实体

- [[scaffolding]] — 已有实体，v3 新增 5 项增强
