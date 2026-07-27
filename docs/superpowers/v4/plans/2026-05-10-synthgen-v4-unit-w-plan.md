SynthGen Core v4 Unit W 实施计划：完备度连续化评分
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit W 设计规范 v1.0
估算：1 周
依赖：v2 #13 执行路由器

---

## 概述

Unit W 交付完备度连续化评分——将约束覆盖程度从布尔判断转化为 0.0-1.0 连续评分。

---

## Task 1：DimensionScore 数据结构

### Step 1.1：维度定义和权重

**产出**：`src/completeness/dimension_score.h`, `src/completeness/dimension_score.cpp`

**验收**：5 个维度定义正确，权重和 = 1.0

### Step 1.2：CompletenessScore 数据结构

**产出**：`src/completeness/completeness_score.h`, `src/completeness/completeness_score.cpp`

**验收**：CompletenessScore 构造正确，is_fully_constrained 当且仅当 score == 1.0

---

## Task 2：CompletenessScorer 核心

### Step 2.1：维度评分计算

**产出**：`src/completeness/completeness_scorer.h`, `src/completeness/completeness_scorer.cpp`

**验收**：每个维度的评分 = constrained_fields / total_fields

### Step 2.2：加权汇总

**验收**：completeness_score = Σ(weight_i × score_i)，结果 ∈ [0.0, 1.0]

### Step 2.3：should_allow_full_function

**验收**：score == threshold 时返回 true，否则 false

---

## Task 3：与执行路由器集成

### Step 3.1：路由器完备度感知

**产出**：修改 `src/router/execution_router.h/cpp`

**验收**：路由器根据完备度评分调整路径选择，低完备度优先走保守路径

### Step 3.2：身份切换与完备度

**验收**：完备度评分影响身份声明——全功能身份要求完备度 >= 1.0

---

## Task 4：脚手架集成

### Step 4.1：Explain 增强

**产出**：修改 `src/explain/explain_engine.h/cpp`

**验收**：Explain 输出包含完备度评分和维度分解

### Step 4.2：Trace 集成

**验收**：评分计算过程记录到 Trace span

### Step 4.3：可观测性指标

**验收**：completeness_score 作为 Prometheus 指标暴露

---

## Task 5：测试

### Step 5.1：错误路径测试清单

**验收**：以下 5 个 ErrorCode 全部有对应测试用例：

| ErrorCode | 测试场景 | 用例数 |
|-----------|---------|--------|
| kInvalidDimensionWeight | 权重 < 0 或权重和 ≠ 1.0 | 2 |
| kScoreOutOfBounds | completeness_score ∉ [0.0, 1.0] | 1 |
| kMissingOrderColumn | 评分维度引用不存在的列 | 1 |
| kDimensionConflict | ENUM 列同时有值域和枚举约束 | 1 |
| kSerializationError | 序列化/反序列化失败 | 1 |

### Step 5.2：功能测试

**产出**：`tests/unit/completeness_scorer_test.cpp`

**验收**：20+ 测试通过，错误测试占比 ≥ 30%

**测试分类**：
- 单元测试：14+（无约束=0.0，仅值域=0.6，值域+条件=0.8，全覆盖=1.0 各 3-4）
- 错误测试：6+（对应上述 ErrorCode）
- 评分精度测试：2（浮点精度验证，边界值 0.9999/1.0001）

### Step 5.3：集成测试

**产出**：`tests/integration/v4_completeness_router_test.cpp`

**验收**：完备度评分正确影响路由器路径选择

**集成场景**：
- 低完备度（<0.6）→ 保守路径
- 高完备度（≥1.0）→ 全功能路径
- 完备度变化时路由器动态切换

---

## 进度追踪

| Task | 估算 | 状态 |
|------|------|------|
| Task 1 | 0.125w | ⬜ |
| Task 2 | 0.25w | ⬜ |
| Task 3 | 0.25w | ⬜ |
| Task 4 | 0.125w | ⬜ |
| Task 5 | 0.25w | ⬜ |
| **合计** | **1w** | — |

---

## 风险

| 风险 | 影响 | 缓解 |
|------|------|------|
| 维度权重争议 | 评分可能不反映实际 | 权重可配置，初始值基于经验 |
| 权重和不等于 1.0 | 评分越界 | 构造时校验，kInvalidDimensionWeight |
| 评分精度不足 | 维度分解失去意义 | 浮点精度 4 位有效数字 |
