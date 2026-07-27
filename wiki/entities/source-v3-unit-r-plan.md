# v3 Unit R Plan — GC Compaction 实施计划

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-unit-r-plan.md
> 编译日期：2026-05-14

## 摘要

Unit R 实施计划，5 个 Task、14 个步骤、估算 1 周。Task 1 实现保护条件框架——ProtectionCondition 枚举、快照引用保护（active_snapshots set）、锚定保护（持久化到存储层）、N 版本内保护（按时间排序取最近 N 个）（0.25w）。Task 2 实现 GcCompactor 核心——compact 合并（先创建新版本→标记旧版本→提交三步策略）、元数据合并保留、自动 compaction 定时触发（0.375w）。Task 3 脚手架集成（0.125w）。Task 4 错误和测试——15+ 单元测试、5+ 边界条件测试（0.125w）。Task 5 集成测试——6+ 与版本链/存储层集成（0.125w）。

## 关键要点

- 保护条件判定：is_protected 遍历所有条件，任一满足返回 true
- 合并三步策略：先创建新版本→标记旧版本为 compacted→提交，中断后以新版本是否创建为准
- 合并元数据保守策略：fidelity_score 取 min、training_rows 取 sum、custom_metadata 后者覆盖
- 不可逆操作防护：explain() 在执行前展示影响预估
- 产出文件：src/storage/gc/ 下的 protection.h/cpp, gc_compactor.h/cpp

## 提取的实体

- [[gc-compaction]] — 已有实体，实施计划补充保护条件和合并策略细节
