# TemplateEngine（组件模板引擎）

> 类型：工具
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

v1 开发辅助工具，给定组件接口描述（JSON），自动生成含 span 创建/写入、metrics 注册/暴露、Explain 接口占位、错误处理框架的 .h/.cpp 骨架。

## 详情

输入格式（JSON）：
- name、namespace、spans（方法名列表）、metrics（指标名列表）
- explain_fields、dependencies、methods（返回类型 + 参数）

输出：完整的 .h 和 .cpp 文件，包含：
- SpanGuard RAII 自动创建/写入
- MetricsRegistry 计数器/直方图注册
- explain() const 方法占位
- Result<T> 错误处理框架
- #pragma once 头文件保护

**不做什么**：不生成核心逻辑代码（约束验证算法、采样策略等），不生成业务逻辑。

验收标准：用模板引擎生成 #8 骨架，人工确认 90%+ 可直接使用。

## v1 范围

v0.1 版本，仅从 #5/#6 提炼模板，支持 #8 骨架生成。

## 关联实体

- [[tool-line]] — 所属工具线
- [[scaffolding]] — 依赖脚手架代码结构
- [[span-guard]] — 自动生成的 span 守卫
- [[metrics-registry]] — 自动生成的指标注册

## 来源

- [[source-v1-unit-i-spec]] — Unit I 设计规范
