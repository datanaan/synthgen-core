# 模型版本链 + GC Compaction

> 类型：组件
> 首次编译：2026-05-11

## 定义

管理模型版本创建、引用、不可变写入，以及自动 compaction 回收旧版本。v3 引入。

## 详情

**版本链（#18）**：
- 版本创建/引用/列表
- 不可变写入
- 版本元数据：训练数据范围、fidelity_score

**GC Compaction（#19）**：
- 3 个保护条件（任一满足则不可回收）：被快照引用、被用户 anchored、最近 N 版本内（N 可配置，默认 10）
- 自动执行，不需用户许可
- 合并后保留元数据：版本清单、训练数据范围、fidelity score、模型参数 hash

**时间旅行退化**：
- 请求已 compaction 的版本时，返回最近可用版本 + 偏差报告
- 偏差报告含 requested/returned/reason/merged_from/fidelity_score_range/version_mismatch

**版本对应**：
- v3 #18：版本链
- v3 #19：GC compaction
- v3 #20：时间旅行（AS OF）
- v3 #24：偏差报告

## 关联实体

- [[storage-engine]] — 模型层是存储引擎的组成部分
- [[drift-evolution]] — 持续对齐创建新版本

## 来源

- [[source-engineering-framework]] — §5.5 模型版本 GC
- [[source-roadmap]] — v3 #18-#24
