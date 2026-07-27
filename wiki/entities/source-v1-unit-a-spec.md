# v1 Unit A Spec — Parser + Type System

> 来源：raw/specs/v1-unit-a-design.md
> 编译日期：2026-05-14

## 摘要
Unit A 是 v1 的地基组件，交付 SynthLang Parser 和 Type System / Schema DDL。Parser 能解析 v1 语法子集（DEFINE TYPE、LOAD DATA、DEFINE CONSTRAINT、GENERATE TABLE），输出 AST；同时识别 v2+ 语法关键字（DURING/WHEN/行间/聚合）并返回 unsupported_in_v1 错误。Type System 支持 FLOAT/INT/DATETIME/STRING/ENUM 五种类型，配合 Schema Registry 提供注册、查询和校验功能。估算 2.5 周，无前置依赖。

## 关键要点
- Parser 采用经典两阶段架构（Lexer + Parser），v1 不支持的语法在 Lexer 阶段识别关键字、Parser 阶段返回错误码 kUnsupportedInV1
- Schema 对象包含列定义、值域范围、ORDER 预留字段，SchemaRegistry 提供注册和查询
- Parser 的脚手架代码（span 创建、metrics 注册、Explain 接口）作为组件模板引擎 v0.1 的素材
- 错误测试用例占比 >= 30%，至少 25 个测试用例

## 提取的实体
- [[synthlang-parser]] — SynthLang 语法的解析器，输出 AST
- [[type-system-schema]] — 类型系统和 Schema DDL，支持五种数据类型及值域范围声明
- [[schema-registry]] — Schema 注册与查询服务，支持校验和类型名唯一性
