SynthGen Core v2 脚手架实施计划
文档性质：脚手架级实施计划
版本：v1.0
日期：2026-05-10
上游文档：v2 脚手架设计规范 v1.0
估算：1 周
依赖：v2 功能组件

---

## 概述

v2 脚手架 5 项增强，与功能组件并行开发。

---

## Task 1：Explain 增强

### Step 1.1：ExplainInfoV2 定义和实现

**产出**：`src/scaffold/explain.h`（扩展）

**验收**：v2 Explain 新增字段可填充

### Step 1.2：各组件 Explain 更新

**做什么**：更新路由器、后筛选、数据引擎的 explain() 方法

**验收**：各组件 explain() 返回 v2 字段

### Step 1.3：Explain 测试

**验收**：5+ Explain 增强测试通过

---

## Task 2：Trace 增强

### Step 2.1：后筛选排除率 span

**做什么**：PostFilter 执行时产生排除率变化子 span

**验收**：后筛选 span 含排除率属性

### Step 2.2：行间/聚合引擎 span

**做什么**：InterRowEngine 和 AggregateEngine 产生详细 span

**验收**：行间/聚合引擎 span 含状态传递信息

### Step 2.3：Trace 测试

**验收**：5+ Trace 增强测试通过

---

## Task 3：可观测性增强

### Step 3.1：新增 Metrics 注册

**做什么**：注册排除率趋势、退化路径命中率、审计验证状态指标

**验收**：`/metrics` 暴露 v2 新增指标

### Step 3.2：Metrics 测试

**验收**：3+ Metrics 增强测试通过

---

## Task 4：错误注入 v2

### Step 4.1：注入场景实现

**做什么**：实现5个错误注入场景

**验收**：5 个注入场景全部正确响应

### Step 4.2：错误注入测试

**验收**：5+ 错误注入测试通过

---

## Task 5：退化路径回归测试

### Step 5.1：5 条路径回归测试

**产出**：`tests/regression/degradation_path_test.cpp`

**验收**：5 条路径各至少 1 个回归测试

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1: Explain | 0.5w | ⬜ |
| Task 2: Trace | 0.5w | ⬜ |
| Task 3: 可观测性 | 0.5w | ⬜ |
| Task 4: 错误注入 | 0.5w | ⬜ |
| Task 5: 回归测试 | 0.5w | ⬜ |
| **合计** | **1w** | — |
