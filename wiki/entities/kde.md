# 核密度估计 (KDE)

> 类型：技术
> 首次编译：2026-05-14
> 最后更新：2026-05-14

## 定义

核密度估计（Kernel Density Estimation）是数据引擎 v1 的核心统计技术，用于从训练数据中学习概率密度分布，提供密度采样、体积比计算和密度估计三大能力。

## 详情

KDE 是一种非参数密度估计方法，通过在每个数据点放置核函数并求和来估计整体分布。

**核心参数**：
- 核函数：gaussian（默认）、epanechnikov、tophat
- 带宽：控制平滑度，默认 Silverman 规则自动选择
  - Silverman 公式：h = (4/(d+2))^(1/(d+4)) * n^(-1/(d+4)) * sigma
  - 可通过 bandwidth_factor 调整（>1 更平滑）
- 维度限制：中低维（<20维）有效，高维有维度灾难问题

**三大能力**：
1. **密度采样**：从学习到的分布中采样新数据（拒绝采样法）
2. **体积比计算**：约束空间体积 / 数据分布体积（蒙特卡洛法，默认 10000 采样点）
3. **密度估计**：在给定点估计概率密度

**限制声明**：
- 维度 >20 返回 kDimensionTooHigh 警告（不拒绝，但精度下降）
- 训练数据量限制：默认最大 100 万行
- 带宽选择：Silverman 规则不适用所有分布，可手动覆盖

## v2 范围

v2 Unit O #15b 实现数据引擎 v1(KDE)：
- DataEngineV1 类：fit()、sample()、volume_ratio()、estimate_density()
- 3 种核函数实现
- Silverman 带宽自动选择
- 蒙特卡洛体积比计算
- 拒绝采样法密度采样
- [COORDINATE] C1：KDE 技术选型待决策（推荐自研 C++ KDE，备选 ONNX Runtime 推理）
- 至少 25 个测试用例

## 关联实体

- [[data-engine]] — 数据引擎 v1 接口
- [[exclusion-rate]] — 体积比用于排除率预估
- [[execution-router]] — 路由器消费 volume_ratio
- [[post-filter]] — 后筛选消费 volume_ratio 预估排除率
- [[evidence-package]] — KDEModelMetadata 写入证据包

## 来源

- [[source-v2-unit-o-spec]] — 三、#15b 数据引擎 v1(KDE)
- [[source-v2-unit-o-plan]] — Part B：#15b 数据引擎 KDE
