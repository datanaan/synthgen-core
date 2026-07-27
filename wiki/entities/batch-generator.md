# BatchGenerator

> 类型：组件
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

批量生成器，将采样请求按 batch_size 分批执行，汇总结果。属于物理引擎的批量调度层。

## 详情

接收 GenerationRequest（含 schema、constraints、limit、seed、distribution、batch_size），按批次调用 RectangularSampler，汇总返回 GenerationResult（ArrowBatch + GenerationStats）。

GenerationStats 包含：rows_generated、rows_requested、exclusion_rate、elapsed_ms、batch_count、distribution_used。

## v1 范围

v1 仅支持矩形域批量生成，exclusion_rate 应为 0.0。

## 关联实体

- [[rectangular-sampler]] — 被调用的底层采样器
- [[seed-controller]] — 种子链，每 batch 使用不同种子
- [[physics-engine]] — 物理引擎整体

## 来源

- [[source-v1-unit-d-spec]] — Unit D 设计规范
