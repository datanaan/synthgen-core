SynthGen Core v2 Unit P 设计规范：DURING/WHEN + EvidencePackage v2
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit P 实施计划
组件：#16 DURING/WHEN 语义 + #17 EvidencePackage v2
估算：1.5 周
依赖：#12 分类器 + #13 路由器 + #14 后筛选 + #15 审计

---

## 一、本 Unit 交付什么

**Unit P 包含两个组件**：

1. **#16 DURING/WHEN 语义**（1 周）：条件约束 + 非矩形约束域处理
2. **#17 EvidencePackage v2**（0.5 周）：v2 新增字段填充

---

## 二、#16 DURING/WHEN 语义

### 2.1 核心语义

DURING/WHEN 是条件约束——约束只在特定条件下生效。

**DURING 语义**：当指定列等于特定值时，约束生效
```
DURING status = "normal" THEN temperature BETWEEN -10 AND 45
// 仅在 status = "normal" 时，温度约束生效
```

**WHEN 语义**：当条件为真时，约束生效
```
WHEN wind_speed > 15 THEN vibration < 2.0
// 仅在风速 >15 时，振动约束生效
```

**非矩形约束域**：

DURING/WHEN 产生的约束域不是简单的矩形空间（BETWEEN/MIN/MAX 定义的），而是**条件性的**。v1 物理引擎不支持非矩形约束域，v2 需要引入拒绝采样/MCMC 处理。

### 2.2 接口定义

```cpp
namespace synthgen::engine::constraint {

// 条件约束类型
enum class ConditionType {
    kDuring,    // DURING column = value
    kWhen,      // WHEN condition THEN constraint
};

// 条件约束定义
struct ConditionalConstraintDef {
    ConditionType condition_type;

    // DURING 字段
    std::string during_column;
    std::string during_value;     // 字符串表示（可转 ENUM/INT/STRING）

    // WHEN 字段
    std::string when_condition;    // 条件表达式（如 "wind_speed > 15"）

    // 条件生效时的约束
    std::vector<ConstraintItem> conditional_constraints;

    // 约束名
    std::string constraint_name;
};

// 条件约束引擎
class ConditionalConstraintEngine {
public:
    explicit ConditionalConstraintEngine(
        const Schema& schema,
        const std::vector<ConditionalConstraintDef>& constraints);

    // 应用条件约束过滤
    Result<ArrowBatch> apply(
        const ArrowBatch& batch,
        const Schema& schema);

    // 在条件约束域内生成
    // 非矩形约束域使用拒绝采样/MCMC
    Result<GenerationResult> generate_in_conditional_domain(
        const Schema& schema,
        const std::vector<ConditionalConstraintDef>& constraints,
        int64_t limit,
        uint64_t seed);

    // Explain
    ExplainInfo explain() const;
};

}  // namespace synthgen::engine::constraint
```

### 2.3 非矩形约束域处理

```
步骤 1：识别 DURING/WHEN 约束
步骤 2：确定条件生效的子空间
步骤 3：在子空间内使用拒绝采样：
  - 物理引擎在值域范围内采样
  - 检查 DURING/WHEN 条件
  - 不满足条件 → 拒绝（丢弃）
  - 满足条件 → 保留
步骤 4：如果拒绝率 >90%，尝试 MCMC
```

### 2.4 错误处理

```cpp
enum class ConditionalConstraintErrorCode {
    kUndefinedColumn,              // DURING 列不存在
    kTypeMismatch,                 // DURING 值与列类型不匹配
    kInvalidCondition,             // WHEN 条件语法错误
    kRejectionSamplingFailed,      // 拒绝采样不收敛
    kMCMCConvergenceFailed,        // MCMC 不收敛
    kConflictingConditions,        // 冲突条件
};
```

---

## 三、#17 EvidencePackage v2

### 3.1 核心语义

EvidencePackage v2 在 v1 基础上新增以下字段：

| 字段 | 适用性 | v2 状态 | 说明 |
|------|--------|---------|------|
| statistical_fidelity | data_engaged | ✅ 数据驱动时填充 | 统计签名 |
| constraint_type_breakdown | aggregation_present | ✅ 填充 | 三类约束计数 |
| generator_identity | always | ✅ 填充 | 身份声明 |
| audit_immutability | always | ✅ verified | v2 起生效 |
| post_filter_info | post_filter_engaged | ✅ 后筛选时填充 | 后筛选信息 |

### 3.2 接口定义

（定义见 v2 阶段设计规范 3.9 节）

### 3.3 v1 → v2 迁移规则

| 字段 | v1 | v2 | 迁移 |
|------|----|----|------|
| schema_version | "v1" | "v2" | 升级 |
| statistical_fidelity | not_applicable | 填充（数据驱动时） | 激活 |
| constraint_type_breakdown | not_applicable | 填充 | 激活 |
| audit_immutability | not_applicable | verified | 激活 |
| post_filter_info | — | 填充（后筛选时） | 新增 |

---

## 四、Unit P 验收标准

### 4.1 #16 DURING/WHEN 功能验收

- [ ] DURING column = value 语法解析正确
- [ ] WHEN condition THEN constraint 语法解析正确
- [ ] 条件约束在条件满足时生效
- [ ] 条件约束在条件不满足时不生效
- [ ] 非矩形约束域的拒绝采样/MCMC 正确
- [ ] 约束分类器正确分类条件约束

### 4.2 #17 EvidencePackage v2 功能验收

- [ ] statistical_fidelity 在数据引擎参与时填充
- [ ] constraint_type_breakdown 三类计数正确
- [ ] generator_identity 每条路径身份正确
- [ ] audit_immutability = "verified"
- [ ] post_filter_info 在后筛选路径时填充
- [ ] v1 兼容：schema_version = "v2" 但 v1 字段保持

### 4.3 错误测试验收

**DURING/WHEN 错误测试**：
- [ ] DURING 列不存在返回 kUndefinedColumn
- [ ] DURING 值与列类型不匹配返回 kTypeMismatch
- [ ] WHEN 条件语法错误返回 kInvalidCondition
- [ ] 拒绝采样不收敛返回 kRejectionSamplingFailed
- [ ] MCMC 不收敛返回 kMCMCConvergenceFailed

**EvidencePackage v2 错误测试**：
- [ ] 缺失 v2 必填字段返回 kSchemaViolation
- [ ] statistical_fidelity 在无数据引擎时 available = false
- [ ] constraint_type_breakdown 与实际分类不一致返回 kConsistencyError

### 4.4 测试验收

- [ ] #16 至少 15 个测试用例
- [ ] #17 至少 10 个测试用例
- [ ] 错误测试占比 ≥ 30%

---

## 五、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `ConditionalConstraintEngine::apply()` | 路由器执行 | 条件约束过滤 |
| `ConditionalConstraintEngine::generate_in_conditional_domain()` | 路由器执行 | 非矩形域生成 |
| `EvidencePackageV2` | SDK/API | 返回给用户 |
