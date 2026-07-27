# v4 Unit X Spec — 反例搜索 + EvidencePackage v3

> 来源：docs/superpowers/v4/specs/2026-05-10-synthgen-v4-unit-x-design.md
> 编译日期：2026-05-14

## 摘要

Unit X 是 v4 的研究性扩展，交付反例搜索（#29 research 里程碑）和 EvidencePackage v3（#30）。反例搜索探索约束系统的"反向验证"——寻找约束未覆盖的区域或约束矛盾的实例，基于待测模型搜索约束边界附近的违反区域。有三种合法状态：available（找到违反区域）、deferred（前置条件未就绪）、research_failed（搜索不收敛）。EvidencePackage v3 继承 v2 所有字段，新增 ModelVersionProvenance（模型训练溯源）、CompletenessScore（完备度评分）、可选 CounterExampleResult（反例搜索结果）。估算 2 周，含 2 个协调项 C5 和 C6。

## 关键要点

- 反例搜索是 research 功能，不是生产功能；deferred 和 research_failed 是合法状态不是错误
- 三种结果应对：available→EvidencePackage 包含反例信息，deferred→不影响其他 v4 功能，research_failed→记录失败原因
- EvidencePackageV3 新增：model_provenance, completeness_score, counter_example(optional), bias_report_ref(optional)
- ModelVersionProvenance 含 model_name, model_version_id, training_data_range, fidelity_score, was_compacted
- C5 协调项：待测模型接入协议（如 v3 未交付则 #29 标记 deferred）
- C6 协调项：反例搜索理论基础（理论框架 v1.3 未包含独立章节，推荐 v4 启动前 1-2 周预研）

## 提取的实体

- [[counter-example-search]] — 反例搜索组件，研究性质的约束系统反向验证
- [[evidence-package-v3]] — EvidencePackage v3，集成模型溯源+完备度评分+反例结果
- [[model-version-provenance]] — 模型版本溯源信息，记录训练来源和保真度
