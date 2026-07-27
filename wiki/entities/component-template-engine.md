# 组件模板引擎

> 类型：工具

## 定义

v1 工具线的核心工具。从已有组件的脚手架代码提炼共同模式，定义为 Inja 模板，然后根据 JSON 接口描述自动生成组件骨架代码（.h + .cpp）。生成的骨架包含 span 创建、metrics 注册、Explain 接口、错误处理框架，核心逻辑处留 TODO。

## 工作流程

```
1. 分析 #5(物理引擎) #6(验证器) 的脚手架代码 -> 识别共同模式
2. 定义 Inja 模板 -> component.h.inja + component.cpp.inja
3. 读取 JSON 接口描述 -> 展开模板 -> 输出 .h + .cpp
```

## 模板变量

| 变量 | 说明 |
|------|------|
| `{{name}}` | 组件名 |
| `{{namespace}}` | 命名空间（如 `synthgen::engine::physics`） |
| `{{spans}}` | span 列表（组件需要创建的 Trace span） |
| `{{metrics}}` | metrics 列表（组件需要注册的指标） |
| `{{explain_fields}}` | Explain 字段 |
| `{{methods}}` | 方法列表 |

## 提炼的模式

- span 创建/写入模式（SpanGuard RAII）
- metrics 注册/暴露模式
- Explain 接口模式
- 错误处理框架（Result<T>）
- 头文件结构（#pragma once + 命名空间）

## 文件位置

- 模板：`tools/scaffold_templates/`
- 引擎：`tools/scaffold_generator/main.cpp`

## v1 验证

用模板引擎生成 EvidencePackage 构建器骨架（#8），验证：
- 骨架可编译
- 骨架通过 CI
- 核心逻辑处留 TODO

## 关联实体

- [[tool-line]] — 组件模板引擎是工具线的核心工具
- [[scaffolding]] — 模板素材来自脚手架代码
- [[span-guard]] — 模板中包含 SpanGuard 模式
- [[metrics-registry]] — 模板中包含 Metrics 注册模式
