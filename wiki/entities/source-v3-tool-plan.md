# v3 工具线 Plan — 模板引擎 v0.3 + 测试辅助 v0.3 + Schema 校验 v1.1 + Trace 分析 v0.2

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-tool-plan.md
> 编译日期：2026-05-14

## 摘要

v3 工具线实施计划分 4 个 Task，估算 0.5 周。Task 1 组件模板引擎 v0.3（新增 version_chain/gc_compactor/time_travel/alignment/model_storage/bias_report 六个模板，输入组件名输出 .h+.cpp+_test.cpp 骨架），Task 2 测试辅助库 v0.3（新增 compaction 一致性宏 ASSERT_CONSISTENT_AFTER_COMPACTION、版本链完整性宏 ASSERT_VERSION_CHAIN_INTEGRITY、漂移检测数据构造函数），Task 3 Schema 校验器 v1.1（新增版本链/GC/EvidencePackage v3 字段校验规则和适用性枚举校验），Task 4 Trace 分析工具 v0.2（新增 4 条规则：compaction 期间版本创建告警、atomic_write 不完整告警、高漂移标红、退化时间旅行标黄）。

## 关键要点

- Task 1：6 个 v3 组件模板，骨架包含命名空间、Result<T>、Explain/Trace 占位
- Task 2：compaction 前后统计一致性检查宏，漂移检测辅助数据构造
- Task 3：ModelVersion/CompactionConfig/CompactionBiasReport 必填字段校验
- Task 4：compaction 冲突检测、不完整 atomic_write 检测、持续高漂移标红
- 总估算 0.5 周，每个 Task 0.125 周

## 提取的实体

- [[component-template-engine]] — 组件模板引擎 v0.3
- [[test-helper-library]] — 测试辅助库 v0.3
- [[schema-compatibility-checker]] — Schema 校验器 v1.1
- [[trace-analyzer]] — Trace 分析工具 v0.2
