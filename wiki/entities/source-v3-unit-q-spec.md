# v3 Unit Q Spec — 模型版本链

> 来源：docs/superpowers/v3/specs/2026-05-10-synthgen-v3-unit-q-design.md
> 编译日期：2026-05-14

## 摘要

Unit Q 是 v3 时间智能的基础，交付模型版本链（#18）——不可变的、链式的版本管理系统。包含 ModelVersion 数据结构（版本元数据含训练数据范围和 fidelity_score）、ModelVersionChain 的创建/引用/列表操作、不可变写入保证。估算 1 周，依赖 v1 #4 存储和元数据层。是 Unit R(GC)、Unit S(时间旅行+持续对齐)、Unit T(增强组件) 的共同前置。

## 关键要点

- 版本链是链式不可变结构：每个版本有唯一 ID 和父版本引用，一旦写入不可修改/删除
- ModelVersion 元数据包含：version_id, model_name, parent_version_id, created_at, created_by, training_data_range, fidelity_score, training_rows, custom_metadata
- 7 个错误码：kVersionNotFound, kParentNotFound, kImmutableViolation, kDuplicateVersionId, kVersionChainCycle, kModelNotFound, kInvalidVersionId
- 至少 15 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[model-version]] — 模型版本数据结构，包含版本元数据和不可变保证
