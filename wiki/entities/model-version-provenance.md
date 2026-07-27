# model-version-provenance

ModelVersionProvenance 是 v4 Unit X 引入的模型版本溯源数据结构，记录生成数据所用模型的完整来源信息。字段包括：model_name（模型名称）、model_version_id（版本标识）、training_data_range（训练数据时间范围）、fidelity_score（保真度评分，范围 [0.0, 1.0]）、was_compacted（是否经过 GC compaction）。与 ModelVersionChain 集成，从版本链自动填充，was_compacted 正确反映 GC 状态。fidelity_score 是参考值而非质量保证。

## 相关文档

- [[source-v4-unit-x-spec]] — Unit X 设计规范
- [[source-v4-unit-x-plan]] — Unit X 实施计划
- [[model-version-chain]] — 模型版本链
