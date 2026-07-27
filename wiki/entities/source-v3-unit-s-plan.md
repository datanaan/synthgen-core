# v3 Unit S Plan — 时间旅行 + 持续对齐实施计划

> 来源：docs/superpowers/v3/plans/2026-05-10-synthgen-v3-unit-s-plan.md
> 编译日期：2026-05-14

## 摘要

Unit S 实施计划，5 个 Task、14 个步骤、估算 2 周。Task 1 实现时间旅行——TimeTravelEngine 核心（query_as_of 版本查找和快照加载）、compaction 退化行为（向上遍历版本链找最近未 compact 版本）、8+ 测试（0.5w）。Task 2 实现漂移检测——DriftDetector（KS 检验为主，逐维 KS + Bonferroni 校正）、6+ 测试（0.25w）。Task 3 实现持续对齐引擎——update_model 主流程、代偿收敛机制（converging/converged/diverging 三态）、增量更新接口 fit_incremental、TestModelProtocol 协议定义、17+ 测试（0.75w）。Task 4 脚手架集成（0.25w）。Task 5 集成测试——8+ 端到端（0.25w）。

## 关键要点

- 时间旅行退化查找：从被请求版本向上遍历版本链，多层 compact 时找最终可用版本
- KS 检验流程：对每个维度独立计算经验 CDF → 计算 KS 统计量 → 与临界值比较（α=0.05）→ 多维用 Bonferroni 校正
- 代偿收敛判定：连续 3 次 drift_score < 0.1 判定收敛，连续 5 次 drift_score > 0.5 判定发散，超时 24h 降级
- 增量更新 KDE：维护训练数据的分箱统计（histogram bins），增量更新 bins；增量 vs 全量 fit 的 KL 散度超阈值时 fallback 到全量 fit
- TestModelProtocol v3 定义 v4 使用：query_density + query_boundary

## 提取的实体

- [[time-travel]] — 已有实体，实施计划补充退化查找算法
- [[continuous-alignment]] — 已有实体，实施计划补充代偿收敛参数
- [[drift-detection]] — 已有实体，实施计划补充 KS 检验流程
- [[test-model-protocol]] — 已有实体，实施计划补充协议定义位置
