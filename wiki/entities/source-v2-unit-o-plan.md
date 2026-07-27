# v2 Unit O Plan — 哈希链审计 + KDE 数据引擎

> 来源：docs/superpowers/v2/plans/2026-05-10-synthgen-v2-unit-o-plan.md
> 编译日期：2026-05-14

## 摘要

Unit O 实施计划分 Part A（审计日志，6 步骤，1 周）和 Part B（数据引擎 KDE，9 步骤，3 周），共 15 步骤，总计 4 周。Part A：WORM 存储、哈希链计算和验证、AuditLog 核心方法。Part B：KDE 核心数学（核函数+带宽选择）、DataEngineV1 实现（fit/sample/volume_ratio/estimate_density）、错误处理和边界条件、25+ 测试。

## 关键要点

- Part A Task A1：WORMStorage 追加写入 Parquet，modify/remove 返回 kWriteOnceViolation
- Part A Task A2：SHA-256 哈希链计算，创世记录 prev_hash="0"，全链验证+断裂/分叉检测
- Part A Task A3：AuditLog 核心方法（create_genesis/append/verify_chain/daily_verification/detect_forks）
- Part B Task B1：高斯核/Epanechnikov 核/Tophat 核 + Silverman 带宽规则
- Part B Task B2：fit（学习 KDE）、sample（拒绝采样法）、volume_ratio（蒙特卡洛法）
- [COORDINATE] C1：KDE 选型未定，按自研 C++ 编写
- [COORDINATE] C8：WORM 选型未定，按带哈希校验 Parquet 编写
- 产出文件：`src/storage/audit/audit_log.h/.cpp`、`src/engine/data/data_engine.h/.cpp`

## 提取的实体

- [[audit-log]] — 哈希链审计实现
- [[worm-storage]] — WORM 存储实现
- [[kde]] — KDE 核心数学和采样
- [[data-engine]] — DataEngineV1 实现
