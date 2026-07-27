# v1 Unit D Spec — Physics Engine v1

> 来源：raw/specs/v1-unit-d-design.md
> 编译日期：2026-05-14

## 摘要

Unit D 实现 v1 物理引擎——在矩形约束域内按基础分布采样，生成物理合法的合成数据。仅支持矩形约束域（BETWEEN/MIN/MAX），不支持非矩形、行间、聚合约束。估算 1.5 周，依赖 Unit A。

## 关键要点

- RectangularSampler：矩形域采样器，支持均匀/高斯分布
- SeedController：三级种子链（全局→请求→batch），保证确定性可复现
- DistributionEngine：基础分布实现（均匀、高斯）
- BatchGenerator：批量生成，支持指定 batch_size
- GenerationStats 返回排除率（v1 纯物理路径应为 0.0）、耗时、batch 数
- 传入非矩形约束返回 unsupported_in_v1 错误

## 提取的实体

- [[rectangular-sampler]] — 矩形域采样器
- [[seed-controller]] — 三级种子控制链
- [[distribution-engine]] — 基础分布实现（均匀/高斯）
- [[physics-engine]] — 物理引擎组件（v1 仅矩形域）
