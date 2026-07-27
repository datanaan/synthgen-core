# evidence-package-v3

EvidencePackage v3 是 v4 Unit X 交付的输出数据结构，在 v2 基础上新增 4 个字段：model_provenance（模型训练溯源信息 ModelVersionProvenance）、completeness_score（完备度连续化评分 CompletenessScore）、counter_example（反例搜索结果 CounterExampleResult）、bias_report_ref（偏差报告引用）。schema_version = "v3"，向后兼容 v2 字段。新增完整性校验：kMissingModelProvenance 和 kMissingCompletenessScore 错误码。

## 相关文档

- [[source-v4-unit-x-spec]] — Unit X 设计规范
- [[source-v4-unit-x-plan]] — Unit X 实施计划
- [[evidence-package]] — EvidencePackage 概念
