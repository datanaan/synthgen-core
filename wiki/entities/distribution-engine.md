# 分布采样引擎 (DistributionEngine)

> 类型：组件

## 定义

物理引擎的分布采样组件。负责根据种子在指定范围内生成符合均匀或高斯分布的采样值。支持 SynthGen 的五种数据类型。

## 支持的数据类型与采样方式

| 类型 | 均匀采样 | 高斯采样 |
|------|---------|---------|
| FLOAT | min + random() * (max - min) | gaussian(mean, stddev) + 截断 |
| INT | floor(min + random() * (max - min + 1)) | gaussian + 截断 + 取整 |
| DATETIME | min_epoch + random() * (max_epoch - min_epoch) | v1 不支持 |
| STRING | 随机长度 [0, max_len]，随机字符 | v1 不支持 |
| ENUM | 从 enum_values 均匀选择 | v1 不支持 |

## 高斯分布参数

- mean = (min + max) / 2
- stddev = (max - min) / 6
- 截断规则：value < min -> min, value > max -> max
- 截断次数统计 -> tail_report

## 统计检验要求

- 均匀分布：Chi-square 检验，p > 0.05（10000 样本）
- 高斯分布：Kolmogorov-Smirnov 检验，p > 0.05（10000 样本）

## 关键类

- `UniformSampler`（`src/engine/physics/uniform_sampler.h/.cpp`）
- `GaussianSampler`（`src/engine/physics/gaussian_sampler.h/.cpp`）

## 关联实体

- [[physics-engine]] — 物理引擎使用分布采样引擎
- [[rectangular-sampler]] — 矩形域采样器调用分布采样引擎
- [[seed-controller]] — 提供确定性随机数生成器
