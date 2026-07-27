# v3 阶段设计规范

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-design.md
> 编译日期：2026-05-14

## 摘要

v3 阶段级约束文档，定义 v3 全部 Unit 的共同基础。v3 引入"时间智能"主题——模型版本管理使每次数据更新产生新模型版本，用户可时间旅行回到任意版本，持续对齐保持模型与最新数据同步。包含 7 个组件（#18-#24）的完整接口定义、依赖图、开发波次、诚实声明、脚手架验收标准、工具验收标准和 3 个协调项（C4 持续对齐接口协议、C5 待测模型接入协议、C10 漂移检测算法选型→已决策 KS 检验）。总估算 6-7 周。

## 关键要点

- v3 依赖图：Q(版本链)→R(GC)→S(时间旅行+持续对齐)→T(增强组件)，脚手架/工具与 Q-T 并行
- 3 个开发波次：W1(Unit Q + T#23存储模型层)、W2(Unit R + T#22,#24)、W3(Unit S + 脚手架/工具)
- 6 条诚实声明：模型版本可追溯、compaction 退化诚实、漂移检测、代偿收敛时限、存储事务原子性、fidelity_mismatch 标记
- C10 决策：漂移检测默认 KS 检验，支持 ks/kl/none，多维用 Bonferroni 校正
- TestModelProtocol 在 v3 定义供 v4 使用

## 提取的实体

- [[time-travel]] — AS OF 时间旅行，按版本读取快照并处理退化
- [[continuous-alignment]] — 持续对齐引擎
- [[compaction-bias-report]] — compaction 退化偏差报告
- [[model-storage-layer]] — 存储模型层
- [[test-model-protocol]] — 待测模型接入协议，v3 定义 v4 使用
