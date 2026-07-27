# SynthGen Core Wiki 知识地图

> 由 LLM Wiki Skill 自动编译维护 | 最后更新：2026-05-14

## 项目概述

SynthGen Core 是一个面向物理世界的智能合成数据生成器。以生成原生数据库内核架构构建，在用户给定的数据世界框架内生成物理合法、统计逼真、完全可追溯的合成数据。当前处于实施阶段，v1-v3 核心代码已完成。

---

## Source 页（原始文档编译）

### 核心框架（7 篇）

| Source | 来源文档 | 核心内容 |
|--------|---------|---------|
| [[source-theory-framework]] | 理论核心框架 v1.3 | 物理优先认识论、约束分层理论、失败模式、身份切换 |
| [[source-engineering-framework]] | 工程框架 v0.4 | 三层架构、约束执行模型、存储引擎、引擎间协议 |
| [[source-roadmap]] | 开发路线图 v1.4 | 30 组件、3 线并行、v1-v4 版本规划 |
| [[source-evidence-package-schema]] | EvidencePackage Schema v1.2 | 数据包结构定义、字段规范、检疫检查清单 |
| [[source-product-positioning]] | 产品定位与协作关系 v2.0 | 条件化价值承诺、Polymorphic-Twin 姊妹系统 |
| [[source-overall-design]] | 整体设计规范 v1.0 | 目录结构、命名约定、技术栈、跨切关注点 |
| [[source-engineering-guidelines]] | 工程执行守则 v1.0 | 三条底线、三个盲区、两条防线 |

### v1 — 基础引擎（18 篇）

| Source | 类型 | 组件 |
|--------|------|------|
| [[source-v1-unit-a-spec]] | Spec | Parser + Type System |
| [[source-v1-unit-a-plan]] | Plan | Parser + Type System |
| [[source-v1-unit-b-spec]] | Spec | Storage Engine |
| [[source-v1-unit-b-plan]] | Plan | Storage Engine |
| [[source-v1-unit-c-spec]] | Spec | Data Import |
| [[source-v1-unit-c-plan]] | Plan | Data Import |
| [[source-v1-unit-d-spec]] | Spec | Physics Engine v1 |
| [[source-v1-unit-d-plan]] | Plan | Physics Engine v1 |
| [[source-v1-unit-e-spec]] | Spec | Validation + tail_report |
| [[source-v1-unit-e-plan]] | Plan | Validation + tail_report |
| [[source-v1-unit-f-spec]] | Spec | EvidencePackage Builder v1 |
| [[source-v1-unit-f-plan]] | Plan | EvidencePackage Builder v1 |
| [[source-v1-unit-g-spec]] | Spec | SDK + REST API |
| [[source-v1-unit-g-plan]] | Plan | SDK + REST API |
| [[source-v1-unit-h-spec]] | Spec | Scaffold v1 |
| [[source-v1-unit-h-plan]] | Plan | Scaffold v1 |
| [[source-v1-unit-i-spec]] | Spec | Tool Line v1 |
| [[source-v1-unit-i-plan]] | Plan | Tool Line v1 |

### v2 — 约束完整（20 篇）

| Source | 类型 | 组件 |
|--------|------|------|
| [[source-v2-unit-j-spec]] / [[source-v2-unit-j-plan]] | Spec/Plan | InterRowConstraint |
| [[source-v2-unit-k-spec]] / [[source-v2-unit-k-plan]] | Spec/Plan | AggregateConstraint |
| [[source-v2-unit-l-spec]] / [[source-v2-unit-l-plan]] | Spec/Plan | ConstraintClassifier |
| [[source-v2-unit-m-spec]] / [[source-v2-unit-m-plan]] | Spec/Plan | ExecutionRouter Refactor |
| [[source-v2-unit-n-spec]] / [[source-v2-unit-n-plan]] | Spec/Plan | PostFilter Complete |
| [[source-v2-unit-o-spec]] / [[source-v2-unit-o-plan]] | Spec/Plan | HashChain + KDE DataEngine |
| [[source-v2-unit-p-spec]] / [[source-v2-unit-p-plan]] | Spec/Plan | DURING/WHEN + EvidencePackage v2 |
| [[source-v2-scaffold-spec]] / [[source-v2-scaffold-plan]] | Spec/Plan | v2 脚手架 |
| [[source-v2-tool-spec]] / [[source-v2-tool-plan]] | Spec/Plan | v2 工具线 |
| [[source-v2-consistency-check]] | 检查 | v2 一致性检查 |
| [[source-v2-completeness-check]] | 检查 | v2 完整性检查 |

### v3 — 时间智能（15 篇）

| Source | 类型 | 组件 |
|--------|------|------|
| [[source-v3-design]] | Spec | v3 阶段设计 |
| [[source-v3-unit-q-spec]] / [[source-v3-unit-q-plan]] | Spec/Plan | ModelVersionChain |
| [[source-v3-unit-r-spec]] / [[source-v3-unit-r-plan]] | Spec/Plan | GC Compaction |
| [[source-v3-unit-s-spec]] / [[source-v3-unit-s-plan]] | Spec/Plan | TimeTravel + ContinuousAlignment |
| [[source-v3-unit-t-spec]] / [[source-v3-unit-t-plan]] | Spec/Plan | TailReport + StorageModel + BiasReport |
| [[source-v3-scaffold-spec]] / [[source-v3-scaffold-plan]] | Spec/Plan | v3 脚手架 |
| [[source-v3-tool-spec]] / [[source-v3-tool-plan]] | Spec/Plan | v3 工具线 |
| [[source-v3-consistency-check]] | 检查 | v3 一致性检查 |
| [[source-v3-completeness-check]] | 检查 | v3 完整性检查 |

### v4 — 高级分析（14 篇）

| Source | 类型 | 组件 |
|--------|------|------|
| [[source-v4-unit-u-spec]] / [[source-v4-unit-u-plan]] | Spec/Plan | ROWS + PARTITION BY |
| [[source-v4-unit-v-spec]] / [[source-v4-unit-v-plan]] | Spec/Plan | SESSION Window |
| [[source-v4-unit-w-spec]] / [[source-v4-unit-w-plan]] | Spec/Plan | Completeness Scoring |
| [[source-v4-unit-x-spec]] / [[source-v4-unit-x-plan]] | Spec/Plan | Counter-example + EvidencePackage v3 |
| [[source-v4-scaffold-spec]] / [[source-v4-scaffold-plan]] | Spec/Plan | v4 脚手架 |
| [[source-v4-tool-spec]] / [[source-v4-tool-plan]] | Spec/Plan | v4 工具线 |
| [[source-v4-consistency-check]] | 检查 | v4 一致性检查 |
| [[source-v4-completeness-check]] | 检查 | v4 完整性检查 |

---

## Entity 页（核心概念/组件）

### 系统与定位

| Entity | 类型 | 说明 |
|--------|------|------|
| [[synthgen-core]] | 项目 | 系统整体定位、使命、边界 |
| [[polymorphic-twin]] | 项目 | 姊妹系统：数字孪生可信治理基础设施 |

### 理论核心

| Entity | 类型 | 说明 |
|--------|------|------|
| [[physics-first]] | 概念 | 物理优先认识论——当物理合法性与统计逼真性冲突时，物理优先 |
| [[three-pillars]] | 概念 | 三大能力支柱：物理驱动、数据驱动、约束驱动（融合） |
| [[constraint-layering]] | 概念 | 三类约束理论分层：值域/行间/聚合 |
| [[failure-modes]] | 概念 | 三种失败模式：基准冲突、约束真空、模仿崩溃 |
| [[identity-switch]] | 概念 | 约束缺失时的身份切换与服务分级 |
| [[data-grade]] | 概念 | 数据等级枚举体系 |
| [[input-honesty]] | 概念 | 输入诚实性的两层保证 |
| [[drift-evolution]] | 概念 | 分布漂移与演化的理论区分 |
| [[honesty-declaration]] | 概念 | 诚实声明机制 |
| [[degradation-path]] | 概念 | 5 条退化路径 + 身份声明 |
| [[two-phase-execution]] | 概念 | 两阶段执行模型 |
| [[exclusion-rate]] | 概念 | 排除率指标 |
| [[constraint-completeness-scoring]] | 概念 | 完备度连续化评分 |

### 接口层组件

| Entity | 类型 | 说明 |
|--------|------|------|
| [[synthlang]] | 组件 | SynthLang 自研 DSL：语法、设计原则 |
| [[synthlang-parser]] | 组件 | SynthLang 递归下降解析器 |
| [[recursive-descent-parser]] | 组件 | 递归下降解析器实现模式 |
| [[type-system-schema]] | 组件 | 类型系统 / Schema DDL |
| [[python-sdk]] | 组件 | Python SDK 客户端 |
| [[rest-api-v1]] | 组件 | REST API v1 端点设计 |
| [[synthgen-service]] | 组件 | C++ HTTP 服务封装 |

### 生成引擎层组件

| Entity | 类型 | 说明 |
|--------|------|------|
| [[execution-router]] | 组件 | 执行路由器：5 条退化路径、身份声明 |
| [[physics-engine]] | 组件 | 物理引擎：矩形域采样(v1)→拒绝采样/MCMC(v2) |
| [[rectangular-sampler]] | 组件 | 矩形域采样器 |
| [[distribution-engine]] | 组件 | 基础分布实现（均匀/高斯） |
| [[batch-generator]] | 组件 | 批量生成调度器 |
| [[seed-controller]] | 组件 | 三级种子控制链（全局→请求→batch） |
| [[data-engine]] | 组件 | 数据引擎：KDE 密度估计(v2) |
| [[kde]] | 技术 | 核密度估计 |
| [[constraint-classifier]] | 组件 | 编译阶段约束分类器 |
| [[inter-row-engine]] | 组件 | 行间约束引擎 |
| [[aggregate-engine]] | 组件 | 聚合约束引擎 |
| [[conditional-constraint]] | 组件 | 条件约束引擎 |
| [[during-when-semantics]] | 概念 | DURING/WHEN 条件约束语义 |
| [[post-filter]] | 组件 | 后筛选保障：排除率预估、运行时保障 |
| [[value-range-validator]] | 组件 | 值域约束验证器（安全网） |
| [[tail-report]] | 组件 | conservative_tail_report：尾部裁剪报告 |
| [[evidence-package]] | 组件 | EvidencePackage 数据包协议 |
| [[evidence-package-v3]] | 组件 | EvidencePackage v3 数据结构 |
| [[frame-buffer]] | 数据结构 | 跨 batch 帧缓冲区 |

### v4 分析组件

| Entity | 类型 | 说明 |
|--------|------|------|
| [[rows-window-engine]] | 组件 | 行数窗口引擎 |
| [[partition-window-engine]] | 组件 | 分组时间窗口引擎 |
| [[session-window-engine]] | 组件 | 会话窗口引擎 |
| [[completeness-scorer]] | 组件 | 完备度评分计算器 |
| [[counter-example-searcher]] | 组件 | 反例搜索引擎（research） |
| [[model-version-provenance]] | 概念 | 模型版本溯源信息 |

### 存储引擎层组件

| Entity | 类型 | 说明 |
|--------|------|------|
| [[storage-engine]] | 组件 | 存储引擎：四层存储、StorageBackend 接口 |
| [[audit-log]] | 组件 | 哈希链审计日志 |
| [[model-version-chain]] | 组件 | 模型版本链 + GC compaction(v3) |
| [[insert-only-base-table]] | 组件 | 基表层 INSERT ONLY 语义 |
| [[snapshot-manager]] | 组件 | 快照管理器 |
| [[worm-storage]] | 组件 | WORM 存储层 |
| [[data-importer]] | 组件 | Parquet 数据导入器 |
| [[schema-compatibility-checker]] | 组件 | Schema 兼容性比对工具 |
| [[metadata-manager]] | 组件 | 元数据管理器 |

### 脚手架与工具

| Entity | 类型 | 说明 |
|--------|------|------|
| [[scaffolding]] | 设施 | 六类脚手架：Explain/Trace/可观测性/测试/CI/错误注入 |
| [[tool-line]] | 设施 | 四个开发辅助工具 |
| [[span-guard]] | 设施 | Trace span RAII 守卫 |
| [[metrics-registry]] | 设施 | 可观测性指标注册器 |
| [[schema-validator]] | 工具 | Schema 校验器 |
| [[trace-analyzer]] | 工具 | Trace 分析工具 |
| [[template-engine]] | 工具 | 组件模板引擎 |
| [[test-helper-library]] | 工具 | 测试辅助库 |
| [[test-helpers]] | 工具 | 测试辅助（宏 + 基类） |
| [[component-template-engine]] | 工具 | Inja 模板引擎集成 |

### 基础设施

| Entity | 类型 | 说明 |
|--------|------|------|
| [[result-pattern]] | 模式 | Result<T> 错误处理模式 |
| [[schema-registry]] | 模式 | Schema 注册表 |
| [[schema-hash]] | 模式 | Schema 哈希 |
| [[evidence-schema-validator]] | 模式 | EvidencePackage Schema 验证器 |
| [[v1-version-gate]] | 模式 | v1 版本门控 |

---

## Topic 页（跨实体综合分析）

| Topic | 说明 |
|-------|------|
| [[topic-architecture]] | 三层架构全景：Interface→Engine→Storage |
| [[topic-version-roadmap]] | v1→v4 版本路线图与依赖关系 |
| [[topic-engineering-principles]] | 工程执行守则：三条底线与两个盲区 |

---

## 统计

| 类别 | 数量 |
|------|------|
| Source 页 | 74 |
| Entity 页 | 71 |
| Topic 页 | 3 |
| **总计** | **148** |
