# 排除率 (Exclusion Rate)

> 类型：概念
> 首次编译：2026-05-11
> 最后更新：2026-05-14

## 定义

排除率是后筛选过程中被过滤掉的行数占总采样行数的比率，是评估约束对采样空间裁剪程度、生成质量和决定退化路径的关键指标。

## 计算公式

```
exclusion_rate = 1 - (constraint_range_width / schema_range_width)
```

- constraint_range_width：约束定义的值域宽度
- schema_range_width：Schema 声明的值域宽度

后筛选语义：
```
exclusion_rate = 被过滤的行数 / 采样总行数
```

## v1 语义

v1 纯物理路径下，estimated_exclusion_rate = 0.0，因为物理引擎直接在约束范围内采样，不需要排除任何已生成的数据。

## v2 排除率分级（ExclusionRateBand）

| 范围 | 分级 | data_grade | 行为 |
|------|------|-----------|------|
| 0-30% | kLow | statistics_guaranteed | 正常后筛选 |
| 30-70% | kMedium | limited_fidelity | 需关注，保守偏向 |
| 70-90% | kHigh | limited_fidelity(保守) | 保守偏向，大量排除 |
| >90% | kCritical | 拒绝 | 不执行后筛选 |

## v2 排除率在系统中的用途

1. **路由决策**：预估排除率决定走哪条退化路径（>90% 拒绝后筛选）
2. **质量评估**：实际排除率与 data_grade 联动，影响输出数据的可信度标记
3. **实时监控**：后筛选过程中排除率变化趋势写入 Trace span 和 Metrics
4. **窗口排除率**：聚合约束引入窗口级排除率，partial_window_excluded 标记不完整窗口

## 排除率预估

- 基于数据引擎的体积比（蒙特卡洛法）
- 数据引擎不可用时保守估计 = 1.0
- 预估与实际偏差目标 <20%

## 特殊情况

| 场景 | 排除率 | 说明 |
|------|--------|------|
| 约束范围 = Schema 范围 | 0.0 | 约束未裁剪采样空间 |
| 约束范围极小 | 接近 1.0 | 约束大幅裁剪采样空间 |
| Schema 无范围声明 | 使用默认值 | 兜底处理 |
| 纯物理路径（v1） | 0.0 | 物理引擎保证采样在约束内 |

## 关联实体

- [[tail-report]] — 排除率是 tail_report 的核心统计指标
- [[physics-engine]] — 物理引擎在 v1 保证 exclusion_rate = 0
- [[data-grade]] — 排除率影响数据等级评定
- [[post-filter]] — 后筛选排除率核心实现
- [[degradation-path]] — 排除率决定退化路径选择
- [[data-engine]] — 体积比用于排除率预估
- [[aggregate-engine]] — 窗口排除率
- [[scaffolding]] — 排除率趋势 Metrics
