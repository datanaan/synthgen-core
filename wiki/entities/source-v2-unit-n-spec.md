# v2 Unit N Spec — 后筛选完整版

> 来源：docs/superpowers/v2/specs/2026-05-10-synthgen-v2-unit-n-design.md
> 编译日期：2026-05-14

## 摘要

Unit N 是 v2 后筛选路径的核心实现。后筛选是执行路由器退化路径之一：物理引擎大量采样后逐行约束过滤，返回满足约束的行。核心功能包括：排除率分级（0-30%/30-70%/70-90%/>90% 四级）、超时截断（30 秒保护）、误差界联动表（排除率与 data_grade 联动）、实时排除率监控。排除率 >90% 拒绝后筛选。估算 1 周，依赖路由器和数据引擎 v1。

## 关键要点

- 后筛选语义：物理采样 → 逐行约束检查 → 过滤不满足的行
- 排除率分级：kLow(0-30%)、kMedium(30-70%)、kHigh(70-90%)、kCritical(>90%)
- 误差界联动表：排除率与 data_grade 映射（statistics_guaranteed / limited_fidelity / 拒绝）
- 超时截断：30 秒保护，截断时返回部分数据 + was_timeout_truncated 标记
- 过采样比：默认 3.0，采样数 = target * ratio
- 实时监控：处理过程中记录排除率变化序列
- 测试要求：至少 20 个测试用例，错误测试占比 >= 30%

## 提取的实体

- [[post-filter]] — 后筛选执行引擎
- [[exclusion-rate]] — 排除率分级与预估
- [[data-grade]] — 数据等级与排除率联动
- [[degradation-path]] — kPostFilter 退化路径
