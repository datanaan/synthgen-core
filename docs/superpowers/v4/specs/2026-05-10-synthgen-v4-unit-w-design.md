SynthGen Core v4 Unit W 设计规范：完备度连续化评分
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v4 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit W 实施计划
组件：#28 完备度连续化评分
估算：1 周
依赖：v2 #13 执行路由器

---

## 一、本 Unit 交付什么

**Unit W 是 v4 诚实性的核心**——完备度从布尔判断走向连续评分，反映约束系统的真实覆盖能力。

交付物：
1. **CompletenessScorer**：基于约束覆盖率的 0.0-1.0 连续评分
2. **DimensionScore**：按维度（值域/行间/聚合/统计签名）的分解评分
3. **is_fully_constrained 语义**：score == 1.0 是特例，不是默认
4. **与执行路由器的集成**：完备度评分影响路径选择

---

## 二、#28 完备度连续化评分

### 2.1 核心语义

完备度评分衡量约束系统对数据空间的覆盖程度：

- **0.0**：无约束，完全自由的数据空间
- **0.5**：部分约束覆盖，有约束空白区域
- **1.0**：完全约束，所有维度都有约束覆盖（特例）

**维度分解**：

| 维度 | 权重 | 说明 |
|------|------|------|
| 值域约束 | 0.3 | 每列是否有 min/max/枚举约束 |
| 行间约束 | 0.2 | 是否有跨行/跨列约束 |
| 聚合约束 | 0.2 | 是否有窗口聚合约束 |
| 统计签名 | 0.2 | 是否有分布/相关性约束 |
| 物理合法性 | 0.1 | 物理约束是否就位（v1 默认 1.0） |

**评分公式**：

```
completeness_score = Σ(dimension_weight_i × dimension_score_i)

dimension_score_i = constrained_fields_i / total_fields_i
```

### 2.2 接口定义

（定义见 v4 阶段设计规范 3.3 节）

### 2.3 错误处理

```cpp
enum class CompletenessErrorCode {
    kEmptySchema,              // 空 Schema 评分 = 0.0（不是错误，是合法状态）
    kInvalidDimensionWeight,   // 维度权重和不等于 1.0
    kUnknownDimension,         // 未知的维度名称
    kConstraintTypeMismatch,   // 约束类型与维度不匹配
    kScoreOutOfRange,          // 评分超出 [0.0, 1.0] 范围
};
```

---

## 三、Unit W 验收标准

### 3.1 功能验收

- [ ] 空 Schema 评分 = 0.0
- [ ] 所有列有值域约束 → 值域维度 = 1.0
- [ ] 有行间约束 → 行间维度 = 1.0
- [ ] 有聚合约束 → 聚合维度 = 1.0
- [ ] 有统计签名 → 统计签名维度 = 1.0
- [ ] 物理合法性默认 = 1.0
- [ ] is_fully_constrained 当且仅当 score == 1.0
- [ ] should_allow_full_function(score, 1.0) 仅当 score == 1.0 返回 true

### 3.2 错误测试验收

- [ ] kEmptySchema（评分 0.0，非错误）
- [ ] kInvalidDimensionWeight
- [ ] kUnknownDimension
- [ ] kConstraintTypeMismatch
- [ ] kScoreOutOfRange

### 3.3 边界条件测试

- [ ] 仅值域约束 → score ≈ 0.3（权重 0.3）
- [ ] 所有维度满分 → score = 1.0
- [ ] 部分列有约束 → 值域维度 ∈ (0, 1)
- [ ] 权重微调对总分的影响
- [ ] 100 列 Schema 的评分性能
- [ ] 评分精度（小数点后 4 位）

### 3.4 测试验收

- [ ] 至少 15 个测试用例
- [ ] 错误测试占比 ≥ 25%

### 3.5 脚手架验收

- [ ] Explain 输出包含完备度评分和维度分解
- [ ] Trace span 记录评分计算过程
- [ ] 完备度评分变化触发可观测性指标

### 3.6 诚实声明验收

- [ ] 布尔判断是 1.0 特例，不是默认
- [ ] 评分不等于质量保证——高评分 ≠ 高质量数据
- [ ] 物理合法性维度始终为 1.0（v1 起就有的保证）

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `CompletenessScorer::score()` | Unit X (EvidencePackage v3) | 生成完备度评分 |
| `CompletenessScore` | 路由器 | 影响路径选择 |
| `should_allow_full_function()` | 路由器 | 判断是否允许全功能模式 |
| `DimensionScore` | Explain | 展示维度分解 |
