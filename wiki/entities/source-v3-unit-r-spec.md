# v3 Unit R Spec — GC Compaction

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-unit-r-design.md
> 编译日期：2026-05-14

## 摘要

Unit R 交付 GC compaction（#19）——将旧版本合并为新版本以节省存储空间，同时保持版本链完整性。核心机制为 3 保护条件（快照引用、用户锚定、N 版本内）确保关键版本不被回收，自动 compaction 定时执行不需用户许可。估算 1 周，依赖 #18 模型版本链。合并后保留关键元数据：版本清单、训练数据范围、fidelity score（取最小值保守估计）、模型参数 hash。

## 关键要点

- 3 保护条件任一满足即不 compact：被快照引用、被用户 anchored、最近 N 版本内（N 可配置，默认 10）
- 合并策略：fidelity_score 取 min（保守），training_rows 取 sum，custom_metadata 后者覆盖
- 自动 compaction 默认每小时检查一次，compaction 进行中时跳过防并发
- 5 个错误码：kCompactionInProgress, kProtectedVersion, kCompactionFailed, kMetadataMergeConflict, kAutoCompactDisabled

## 提取的实体

- [[gc-compaction]] — GC compaction 组件，自动合并旧版本并保留关键元数据
