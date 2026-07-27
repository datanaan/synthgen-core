# v1 Unit I Spec — Tool Line v1

> 来源：raw/specs/v1-unit-i-design.md
> 编译日期：2026-05-14

## 摘要

Unit I 实现 v1 两项开发辅助工具：组件模板引擎 v0.1 和测试辅助库 v0.1。工具线在第 4 周起引入，依赖已有组件的脚手架代码作为素材。估算 0.5 周，依赖 Unit D + Unit E。

## 关键要点

- 组件模板引擎 v0.1：给定组件接口描述（JSON），自动生成含 span、metrics、Explain、错误处理的 .h/.cpp 骨架
- 不生成核心逻辑代码，只生成脚手架骨架
- 测试辅助库 v0.1：参数化值域测试宏（SYNTHGEN_VALUE_RANGE_TEST）+ 种子固定测试基类（SeededTestFixture）
- 模板引擎输入格式：JSON 描述 name、namespace、spans、metrics、explain_fields、dependencies、methods
- 验收：用模板引擎生成 #8 骨架，人工确认 90%+ 可直接使用

## 提取的实体

- [[tool-line]] — 四个开发辅助工具
- [[scaffolding]] — 脚手架设施（模板引擎依赖其代码结构）
