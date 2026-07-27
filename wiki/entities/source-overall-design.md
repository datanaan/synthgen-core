# 整体设计规范 v1.0

> 来源：docs/superpowers/2026-05-10-synthgen-overall-design.md
> 编译日期：2026-05-11

## 摘要

项目级架构约束文档，定义跨所有版本的技术决策：三层架构分层规则、目录结构、命名约定、技术栈（C++17/CMake/Google Test/Arrow/Parquet）、跨切关注点（Trace/Explain/Metrics/错误处理/种子控制）、测试策略、EvidencePackage Schema 版本策略。

## 关键要点

- **单向依赖**：Interface→Engine→Storage，禁止反向依赖，同层可依赖
- **命名约定**：类 PascalCase、函数 snake_case、常量 kPascalCase、命名空间 synthgen::模块、头文件 #pragma once + Result<T>
- **技术栈**：C++17 + CMake 3.20+ + Google Test + Arrow/Parquet + ONNX Runtime + pybind11 + crow.h
- **跨切关注点**：Trace（RAII SpanGuard）、Explain（explain() const 方法）、Metrics（进程内计数器→Prometheus）、错误处理（Result<T> 不抛异常）、种子控制（hash 派生确定性）
- **测试策略**：错误路径占比≥30%、标准数据集 sensor_1000.parquet、固定 seed→参考快照自动比对
- **Schema 版本策略**：字段适用性标注 always/data_engaged/aggregation_present/drift_available/not_applicable

## 提取的实体

- [[synthgen-core]] — 项目整体
- [[scaffolding]] — 脚手架设施规范
- [[evidence-package]] — Schema 版本策略
- [[storage-engine]] — 目录结构
