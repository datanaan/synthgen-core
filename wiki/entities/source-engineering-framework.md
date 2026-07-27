# 工程框架 v0.4

> 来源：docs/core/SynthGen Core 工程框架 v0.4.md
> 编译日期：2026-05-11

## 摘要

定义 SynthGen Core "生成原生数据库"的三层架构（接口层/生成引擎层/存储引擎层）、SynthLang DSL 语法、三类约束执行模型、执行路由器退化路径、后筛选运行时保障、存储引擎四层结构、哈希链审计日志、引擎间协议。经四轮对抗性审查通过。

## 关键要点

- **三层架构**：接口层→生成引擎层→存储引擎层，单向依赖
- **SynthLang**：自研 DSL，不模仿 SQL，含 DEFINE TYPE / LOAD DATA / DEFINE CONSTRAINT / GENERATE TABLE 语法
- **三类约束执行**：值域逐行→行间 batch 有状态→聚合两阶段，优先级不可跳过
- **执行路由器**：5 条退化路径（全功能/后筛选/纯物理/统计生成/KDE扰动）
- **后筛选**：排除率>80%保守偏向，>90%拒绝后筛选；排除率与 data_grade 联动
- **存储引擎**：StorageBackend 抽象接口，基表层(INSERT ONLY)+快照层(不可变)+模型层(版本链)+审计日志(哈希链)
- **引擎间协议**：查询协议、生成协议（流式返回+背压）、数据协议（atomic_write 事务）
- **错误处理默认策略**：不回退，不重试，不静默

## 提取的实体

- [[synthlang]] — SynthLang 自研 DSL
- [[execution-router]] — 执行路由器与退化路径
- [[post-filter]] — 后筛选保障机制
- [[storage-engine]] — 存储引擎架构
- [[audit-log]] — 哈希链审计日志
- [[constraint-classifier]] — 编译阶段约束分类器
- [[physics-engine]] — 物理引擎
- [[data-engine]] — 数据引擎
- [[evidence-package]] — EvidencePackage 构建器
