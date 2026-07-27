# v1 Unit D Plan — Physics Engine v1

> 来源：docs/superpowers/v1/plans/2026-05-10-synthgen-v1-unit-d-plan.md
> 编译日期：2026-05-14

## 摘要

Unit D 实现 v1 物理引擎——矩形域采样器（RectangularSampler），估算 1.5 周，依赖 Unit A。包含 5 个 Task、15 个步骤：SeedController（global_seed -> request_seed -> batch_seed -> row_seed 派生链）、DistributionEngine（均匀/高斯分布采样，支持 FLOAT/INT/DATETIME/STRING/ENUM）、RectangularSampler（从约束提取采样范围，批量生成 ArrowBatch）、确定性验证（参考快照比对）、脚手架集成。核心是在 BETWEEN/MIN/MAX 定义的超矩形内按均匀/高斯分布采样。

## 关键要点

- 种子派生链保证确定性：相同 global_seed + 相同 request_id -> 相同输出
- 高斯分布含截断：value < min -> min, value > max -> max，截断次数记入 tail_report
- 采样范围从约束中提取，多约束取交集，无约束列使用 Schema 默认范围
- 使用 std::mt19937_64（Mersenne Twister），跨平台行为一致
- 参考快照验证确定性：seed=42 生成 1000 行 -> 保存快照 -> 后续比对

## 实现细节

### 关键类

| 类/结构 | 文件路径 | 职责 |
|---------|---------|------|
| `SeedController` | `src/engine/physics/seed_controller.h/.cpp` | 种子派生链：global -> request -> batch -> row |
| 随机数生成器 | `src/engine/physics/random.h/.cpp` | 基于 std::mt19937_64 的确定性 RNG |
| `UniformSampler` | `src/engine/physics/uniform_sampler.h/.cpp` | 均匀分布采样（5 种数据类型） |
| `GaussianSampler` | `src/engine/physics/gaussian_sampler.h/.cpp` | 高斯分布采样（含截断） |
| `RangeExtractor` | `src/engine/physics/range_extractor.h/.cpp` | 从约束提取每列采样范围 |
| `BatchGenerator` | `src/engine/physics/batch_generator.h/.cpp` | 批量生成，每 batch 独立种子 |
| `RectangularSampler` | `src/engine/physics/rectangular_sampler.h/.cpp` | 物理引擎入口，集成所有组件 |

### 种子派生链

```
global_seed -> request_seed(request_id) -> batch_seed(batch_index) -> row_seed(row_index)
```

- 确定性：相同输入 -> 相同输出
- 独立性：不同 batch 的种子不相关
- 使用 std::hash 或自定义哈希函数

### 分布采样算法

| 类型 | 均匀采样 | 高斯采样 |
|------|---------|---------|
| FLOAT | min + random() * (max - min) | gaussian(seed, (min+max)/2, (max-min)/6) + 截断 |
| INT | floor(min + random() * (max - min + 1)) | 类似 FLOAT + 取整 |
| DATETIME | min_epoch + random() * (max_epoch - min_epoch) | - |
| STRING | 随机长度 [0, max_len]，随机字符 | - |
| ENUM | 从 enum_values 均匀选择 | - |

### 测试策略

- 种子控制测试 10+ 用例（含 Chi-square 均匀分布检验、KS 高斯分布检验）
- 分布测试 15+ 用例（错误测试 >= 30%）
- 采样器集成测试 15+ 用例（错误测试 >= 30%）
- 确定性测试 4+ 用例（快照比对）
- 统计检验使用 10000 样本 + 固定种子

### Explain 信息

```cpp
ExplainInfo {
    .execution_mode = ExecutionMode::kRowByRow,
    .path = "physics_sampling",
    .constraint_classification = {value_range: N, inter_row: 0, aggregate: 0},
    .estimated_exclusion_rate = 0.0  // v1 纯物理路径
};
```

## 提取的实体

- [[physics-engine]] — 物理引擎（已存在）
- [[seed-controller]] — 种子派生链控制器，保证生成确定性（新实体）
- [[rectangular-sampler]] — 矩形域采样器，v1 物理引擎的核心实现（新实体）
- [[distribution-engine]] — 分布采样引擎，支持均匀和高斯分布（新实体）
