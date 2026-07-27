# v3 脚手架 Plan — Explain + Trace + 可观测性 + 错误注入 + 一致性测试

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-scaffold-plan.md
> 编译日期：2026-05-14

## 摘要

v3 脚手架实施计划分 5 个 Task，估算 1 周。Task 1 Explain 增强（版本链/GC/时间旅行/持续对齐四个组件的 explain() 扩展），Task 2 Trace 增强（版本链/GC/时间旅行/持续对齐四个组件的 span 定义，含漂移检测和 atomic_write 三阶段子 span），Task 3 可观测性增强（版本链/GC/存储模型层/持续对齐四大类 Metrics），Task 4 错误注入增强（compaction 中断/并发快照引用/atomic_write 中断/漂移超时四个场景），Task 5 compaction 前后一致性测试（值域/行间/聚合/KDE 四类约束的 compaction 一致性验证）。

## 关键要点

- Task 1：四个 v3 组件 Explain 信息定义，compaction 前可预览影响
- Task 2：漂移检测结果在 span 中可查，atomic_write 产生 3 个子 span（data/meta/audit）
- Task 3：version_chain_total_versions、gc_compaction_total、alignment_drift_score 等指标
- Task 4：compaction 写入中途失败、并发快照引用冲突、atomic_write 阶段二中断、漂移检测超时
- Task 5：compaction 后生成结果与直接生成结果的统计一致性（均值 <1%，方差 <5%）
- 总测试用例 19+（Explain 4+，Trace 4+，Metrics 3+，错误注入 4+，一致性 4+）

## 提取的实体

- [[scaffolding]] — 脚手架暗线实施
- [[model-version-chain]] — 版本链 Explain/Trace 增强
- [[metrics-registry]] — v3 可观测性指标注册
