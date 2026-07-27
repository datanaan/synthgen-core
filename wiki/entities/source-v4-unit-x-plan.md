# v4 Unit X Plan — 反例搜索(research) + EvidencePackage v3

> 来源：docs/superpowers/v4/plans/2026-05-10-synthgen-v4-unit-x-plan.md
> 编译日期：2026-05-14

## 摘要

Unit X 实施计划分 5 个 Task，估算 2 周，依赖 Unit W（#28）、v2 #13 执行路由器和待测模型接入协议。标注 [COORDINATE] C5（待测模型接入协议）和 C6（反例搜索理论基础），如未解决则 #29 部分标记 deferred 仅交付 #30。Task 1 实现 ModelVersionProvenance（model_name/version_id/fidelity_score/was_compacted，与版本链集成）。Task 2 条件执行 CounterExampleSearcher（三态 available/deferred/research_failed，搜索不收敛/超时错误处理，前置条件未就绪时返回 deferred 最小实现）。Task 3 实现 EvidencePackage v3（继承 v2 字段 + 新增 model_provenance/completeness_score/counter_example/bias_report_ref）。Task 4 脚手架集成（Explain/Trace/可观测性）。Task 5 测试（9 个 ErrorCode、22+ 测试、deferred 场景测试、v2→v3 兼容性测试）。

## 关键要点

- ModelVersionProvenance：fidelity_score 在 [0.0, 1.0] 范围，was_compacted 反映 GC 状态
- CounterExampleSearcher 条件执行：C5/C6 至少一项就绪才实现搜索核心
- CounterExampleResult 三态：available（找到违反区域）/ deferred（前置未就绪）/ research_failed
- EvidencePackage v3：继承 v2，新增 4 个字段，schema_version = "v3"
- deferred 路径：search() 直接返回 status="deferred"，不影响其他字段
- 9 个 ErrorCode 覆盖搜索/序列化/兼容性/deferred 场景
- 22+ 测试（EPv3 12+，反例搜索 10+），v2→v3 升级无数据丢失
- 风险：C5/C6 未解决导致 #29 deferred、搜索不收敛、v2→v3 不兼容

## 提取的实体

- [[counter-example-searcher]] — 反例搜索引擎（research 里程碑）
- [[evidence-package-v3]] — EvidencePackage v3 数据结构
- [[model-version-provenance]] — 模型版本溯源信息
