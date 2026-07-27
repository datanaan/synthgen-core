# v1 Unit G Spec — SDK + REST API

> 来源：raw/specs/v1-unit-g-design.md
> 编译日期：2026-05-14

## 摘要

Unit G 实现用户接口——Python SDK 和 REST API。SynthLang 为内部编译目标，v1 不直接对用户暴露。Python SDK 面向数据科学家，REST API 供其他服务调用，通过 pybind11 绑定 C++ 内核。估算 1 周，依赖 Unit F。

## 关键要点

- Python SDK：SynthGenClient 类，提供 define_type、load_data、define_constraint、explain、generate 方法
- REST API 端点：POST /v1/types、POST /v1/data/load、POST /v1/constraints、POST /v1/generate、GET /v1/explain
- pybind11 绑定：C++ 核心暴露到 Python
- Schema 定义通过 Column 类声明式构建
- explain() 方法返回执行计划预览（执行模式、路径、约束分类）

## 提取的实体

- [[python-sdk]] — Python SDK 客户端
- [[rest-api-v1]] — REST API v1 端点设计
- [[synthlang-parser]] — SynthLang 解析器（内部编译目标）
