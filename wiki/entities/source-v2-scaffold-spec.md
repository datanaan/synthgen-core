# v2 脚手架 Spec — 暗线增强

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-scaffold-design.md
> 编译日期：2026-05-14

## 摘要

v2 脚手架在 v1 最小版基础上增加 5 项增强，与 v2 功能组件同版本交付。Explain 增强：新增排除率预估、体积比、数据来源、退化路径选择等字段。Trace 增强：后筛选路径实时排除率变化记录。可观测性增强：排除率趋势、退化路径命中率、审计验证状态三项 Metrics。错误注入 v2：5 个新增注入场景（后筛选排除率爆炸、数据引擎故障等）。测试增强：5 条退化路径各一个回归测试。

## 关键要点

- Explain 增强：ExplainInfoV2 新增 exclusion_rate_estimate、volume_ratio、data_source、selected_path 等字段
- Trace 增强：后筛选 span 含排除率属性，行间/聚合引擎分别产生子 span
- 可观测性：7 个新增 Metrics（排除率趋势、退化路径命中率、审计验证状态）
- 错误注入 v2：5 个场景（排除率爆炸、数据引擎不可用/fit 失败、审计写入失败、哈希链断裂）
- 退化路径回归测试：5 条路径各 1 个端到端测试 + EvidencePackage 验证

## 提取的实体

- [[scaffolding]] — 脚手架工程暗线
- [[degradation-path]] — 退化路径回归测试
- [[exclusion-rate]] — 排除率趋势 Metrics
- [[audit-log]] — 审计验证状态 Metrics
