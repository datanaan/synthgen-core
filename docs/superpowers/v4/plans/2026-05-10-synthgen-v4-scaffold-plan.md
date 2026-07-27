SynthGen Core v4 脚手架实施计划：Explain 增强 + Trace 增强
文档性质：脚手架级实施计划
版本：v1.0
日期：2026-05-10
上游文档：v4 脚手架设计规范 v1.0
估算：0.5 周

---

## 概述

v4 脚手架交付 Explain 增强（完备度+模型溯源+反例状态）和 Trace 增强（评分变化+搜索轨迹）。

---

## Task 1：Explain v4 增强

### Step 1.1：完备度评分展示

**产出**：修改 `src/explain/explain_engine.h/cpp`

**验收**：Explain 输出包含 completeness.score 和 dimensions 分解

### Step 1.2：模型溯源展示

**验收**：Explain 输出包含 model_provenance 字段

### Step 1.3：反例搜索状态展示

**验收**：Explain 输出包含 counter_example_search.status，deferred 不报错

---

## Task 2：Trace v4 增强

### Step 2.1：完备度评分 Trace

**产出**：修改 `src/trace/` 相关文件

**验收**：completeness_score_v4 span 正确记录

### Step 2.2：反例搜索 Trace（条件执行）

**验收**：counter_example_search_v4 span 正确记录（如反例搜索执行）

---

## Task 3：可观测性

### Step 3.1：Prometheus 指标

**产出**：修改 `src/metrics/` 相关文件

**验收**：completeness_score 和 counter_example_status 指标暴露

---

## Task 4：测试

### Step 4.1：功能测试

**产出**：`tests/unit/v4_scaffold_test.cpp`

**验收**：8+ 测试通过

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1 | 0.125w | ⬜ |
| Task 2 | 0.125w | ⬜ |
| Task 3 | 0.125w | ⬜ |
| Task 4 | 0.125w | ⬜ |
| **合计** | **0.5w** | — |
