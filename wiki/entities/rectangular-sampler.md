# 矩形域采样器 (RectangularSampler)

> 类型：组件

## 定义

v1 物理引擎的核心实现。在 BETWEEN/MIN/MAX 定义的超矩形内按均匀或高斯分布采样，生成符合约束的合成数据。是 v1 唯一的采样路径。

## 核心接口

```cpp
// src/engine/physics/rectangular_sampler.h
class RectangularSampler {
public:
    Result<GenerationResult> generate(const GenerationRequest& request);
    ExplainInfo explain(const GenerationRequest& request) const;
    Result<void> validate_request(const GenerationRequest& request) const;
};
```

## 工作流程

1. RangeExtractor 从约束中提取每列的采样范围（多约束取交集）
2. 无约束列使用 Schema 默认范围
3. BatchGenerator 按 batch_size 分批生成，每 batch 使用独立种子
4. DistributionEngine 执行实际采样（均匀或高斯）
5. 组装为 ArrowBatch 返回

## v1 限制

- 只支持值域约束（矩形域）
- DURING/WHEN/行间/聚合约束 -> `kUnsupportedInV1`
- estimated_exclusion_rate = 0.0（纯物理路径，无排除）

## Explain 输出

```cpp
{
    .execution_mode = ExecutionMode::kRowByRow,
    .path = "physics_sampling",
    .constraint_classification = {value_range: N, inter_row: 0, aggregate: 0},
    .distribution = request.distribution,
    .estimated_exclusion_rate = 0.0
};
```

## 关联实体

- [[physics-engine]] — 物理引擎的 v1 实现
- [[seed-controller]] — 提供确定性种子
- [[distribution-engine]] — 执行实际分布采样
- [[tail-report]] — 记录高斯截断统计
