# v3 Unit T Spec — TailReport 增强 + 存储模型层 + 偏差报告

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-unit-t-design.md
> 编译日期：2026-05-14

## 摘要

Unit T 交付三个 v3 增强组件：tail_report 增强版（#22，排除率与 data_grade 联动 + fidelity_mismatch 标记 + 代偿模型状态）、存储模型层（#23，检查点存储 + 流式加载 + atomic_write 两阶段提交事务）、偏差报告（#24，compaction 偏差字段完整，证明链可重建）。估算 2.5 周，依赖 v2#14 后筛选 + #21 持续对齐 + #19 GC + #18 版本链。注意：误差界数据在 v2 的 statistical_fidelity 中已有，#22 是呈现增强非新增计算。

## 关键要点

- TailReportV3 继承 TailReportV1，新增 rate_band、data_grade、fidelity_mismatch、mismatch_reason、compensation_status、compensation_deadline
- 存储模型层 atomic_write 三阶段提交：写数据(临时文件) → 写元数据(版本链注册) → 提交审计，中断恢复以元数据层为权威源
- CompactionBiasReport 完整字段：requested_version, returned_version, reason, merged_from, training_data_range, fidelity_score_range_min/max, version_mismatch
- 至少 20 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[model-storage-layer]] — 存储模型层，检查点存储+流式加载+atomic_write 事务
- [[compaction-bias-report]] — compaction 退化偏差报告，记录版本偏差并保证证明链可重建
