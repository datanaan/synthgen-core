SynthGen Core v2 Unit N 实施计划：后筛选完整版
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit N 设计规范 v1.0
估算：1 周
依赖：#13 执行路由器 + #15b 数据引擎 v1

---

## 概述

Unit N 交付后筛选完整版——排除率预估、超时截断、误差界联动表、实时排除率监控。

---

## Task 1：误差界联动表

**目标**：实现排除率分级和 data_grade 映射

### Step 1.1：ExclusionRateBand 和映射实现

**做什么**：实现排除率分级和联动映射

**产出**：`src/engine/postfilter/exclusion_band.h`, `src/engine/postfilter/exclusion_band.cpp`

**验收**：
- [ ] 4 级分级正确
- [ ] data_grade 映射正确
- [ ] 边界值（30%/70%/90%）正确

### Step 1.2：联动表测试

**产出**：`tests/unit/exclusion_band_test.cpp`

**验收**：8+ 测试通过

---

## Task 2：后筛选执行

**目标**：实现后筛选核心逻辑

### Step 2.1：核心筛选逻辑

**做什么**：实现逐行约束检查 + 过滤

**产出**：`src/engine/postfilter/post_filter.h`, `src/engine/postfilter/post_filter.cpp`

**关键逻辑**：
- 对采样数据逐行检查值域+行间+聚合约束
- 过滤不满足约束的行
- 计算排除率

**验收**：
- [ ] 过滤逻辑正确
- [ ] 排除率计算正确

### Step 2.2：排除率预估

**做什么**：实现基于数据引擎的排除率预估

**关键逻辑**：
- 调用 DataEngineV1::volume_ratio()
- volume_ratio → 排除率映射

**验收**：
- [ ] 预估与实际偏差 <20%
- [ ] 预估失败时保守估计

### Step 2.3：超时截断

**做什么**：实现超时保护

**关键逻辑**：
- 记录开始时间
- 每处理 1000 行检查是否超时
- 超时时返回已过滤的部分数据 + was_timeout_truncated = true

**验收**：
- [ ] 超时截断正确
- [ ] 截断时返回部分数据

### Step 2.4：实时排除率监控

**做什么**：实现处理过程中排除率变化记录

**关键逻辑**：
- 每处理 N 行记录当前排除率
- 存储到 realtime_exclusion_rate_series

**验收**：
- [ ] 排除率变化序列记录正确

---

## Task 3：错误处理和边界条件

### Step 3.1：错误路径实现

**验收**：全部 PostFilterErrorCode 有处理

### Step 3.2：边界条件测试

**产出**：`tests/unit/postfilter_boundary_test.cpp`

**验收**：8+ 边界条件测试通过

---

## Task 4：集成测试

### Step 4.1：端到端后筛选测试

**产出**：`tests/integration/postfilter_integration_test.cpp`

**验收**：8+ 集成测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 联动表 | 2 | 0.125w | ⬜ |
| Task 2: 核心实现 | 4 | 0.5w | ⬜ |
| Task 3: 错误/边界 | 2 | 0.125w | ⬜ |
| Task 4: 集成测试 | 1 | 0.25w | ⬜ |
| **合计** | **9** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 排除率预估不准确 | 保守偏向，宁可高估排除率 |
| 超时截断数据不完整 | 明确标记 was_timeout_truncated，用户可判断 |
| 实时监控影响性能 | 每 1000 行才记录一次，开销可忽略 |
