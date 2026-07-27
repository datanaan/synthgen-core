# v3 Unit S Spec — 时间旅行 + 持续对齐

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-unit-s-design.md
> 编译日期：2026-05-14

## 摘要

Unit S 交付时间旅行（#20 AS OF）和持续对齐（#21 UPDATE MODEL）——v3 的核心时间智能能力。时间旅行允许用户按版本 ID 查询任意模型版本数据，compaction 退化时返回最近可用版本加偏差报告。持续对齐保持模型与最新数据同步：新数据到来后执行漂移检测（默认 KS 检验），触发代偿收敛机制。估算 2 周，依赖 #18 版本链 + #19 GC + v2#13 执行路由器 + v2#15b 数据引擎。含两个协调项：C4（增量更新接口）和 C5（待测模型接入协议）。

## 关键要点

- 时间旅行退化行为：请求版本已被 compact 时返回最近可用版本 + CompactionBiasReport（was_degraded=true）
- 持续对齐流程：加载当前版本 → 读取新数据 → 漂移检测 → 增量更新(fit_incremental) → 创建新版本
- 代偿收敛机制：converging → converged / diverging，连续 N 次 drift_score < 阈值判定收敛，超时降级
- 漂移检测默认 KS 检验（C10 决策），支持 ks/kl/none 三种模式，多维用逐维 KS + Bonferroni 校正

## 提取的实体

- [[time-travel]] — AS OF 时间旅行，按版本读取快照并处理 compaction 退化
- [[continuous-alignment]] — 持续对齐引擎，漂移检测+代偿收敛+增量更新
- [[drift-detection]] — 漂移检测机制，KS 检验为默认算法
- [[compensation-mechanism]] — 代偿收敛机制，管理模型更新后的收敛/发散/超时降级状态
