# v3 Unit T Plan — TailReport 增强 + 存储模型层 + 偏差报告实施计划

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-unit-t-plan.md
> 编译日期：2026-05-14

## 摘要

Unit T 实施计划，5 个 Task、13 个步骤、估算 2.5 周，跨 Wave 1-2。Task 1 实现 tail_report 增强版——TailReportV3 结构体扩展（继承 V1 新增 rate_band/data_grade/fidelity_mismatch/compensation_status）、联动逻辑实现（排除率联动、fidelity_mismatch 从 TimeTravelResult.was_degraded 联动、compensation_status 从 AlignmentResult 传入）、8+ 测试（0.5w）。Task 2 实现存储模型层——检查点存储（Parquet 写入）、流式加载（分批读取+LRU 缓存默认 5 个）、atomic_write 两阶段提交（三阶段 span）、版本索引、10+ 测试（1w）。Task 3 实现偏差报告——CompactionBiasReport 完整字段和证明链可重建、5+ 测试（0.25w）。Task 4 脚手架集成（0.25w）。Task 5 集成测试——8+ 端到端（0.5w）。

## 关键要点

- 排除率联动：0-30%→rate_band=low/data_grade=statistics_guaranteed，30-70%→medium/limited_fidelity
- atomic_write 中断恢复策略：阶段一中断清理临时文件，阶段二中断清理无注册数据文件，阶段三中断补写审计日志；以元数据层为权威源
- 检查点大小预估：20 维 KDE 约 10MB，大模型 >1GB 时流式加载 + LRU 缓存
- 偏差报告证明链：merged_from → 请求版本 → 返回版本的完整路径可追溯
- 产出文件：src/engine/evidence/tail_report.h/cpp（扩展）、src/storage/model/、src/storage/gc/compaction_bias_report.h/cpp

## 提取的实体

- [[model-storage-layer]] — 已有实体，实施计划补充 atomic_write 中断恢复策略
- [[compaction-bias-report]] — 已有实体，实施计划补充证明链追溯机制
