SynthGen Core v2 Unit L 设计规范：约束分类器
文档性质：Unit 级设计规范
版本：v1.0
日期：2026-05-10
上游文档：v2 阶段设计规范 v1.0、整体设计规范 v1.0
下游文档：Unit L 实施计划
组件：#12 约束分类器（编译时）
估算：1 周
依赖：Parser 扩展

---

## 一、本 Unit 交付什么

**Unit L 是 v2 执行路由器的前置**——约束分类器在编译时确定约束类型和执行模式。

交付物：
1. **ConstraintClassifier**：编译时约束分类（值域/行间/聚合）
2. **ClassificationResult**：分类结果 + 执行模式标记
3. **PHASE 标记**：PHASE_ONE/PHASE_TWO 执行阶段标记
4. **ExecutionMode**：row_by_row / stateful_batch / two_phase

---

## 二、#12 约束分类器

### 2.1 核心语义

约束分类器的职责是**在执行前确定约束组合的执行模式**，这是执行路由器做路由决策的基础。

**分类规则**：

| 约束组合 | 执行模式 | 说明 |
|---------|---------|------|
| 仅值域约束 | kRowByRow | 逐行检查，无状态 |
| 含行间约束 | kStatefulBatch | batch 有状态，跨 batch 传递 |
| 含聚合约束 | kTwoPhase | 两阶段执行 |
| 行间 + 聚合 | kTwoPhase | 聚合优先级最高 |

**PHASE 标记规则**：
- 值域约束 → PHASE_ONE
- 行间约束 → PHASE_ONE
- 聚合约束 → PHASE_TWO

### 2.2 接口定义

```cpp
namespace synthgen::engine::router {

// 约束类型
enum class ConstraintType {
    kValueRange,      // 值域约束：BETWEEN/MIN/MAX
    kInterRow,        // 行间约束：column[t] - column[t-1] < delta
    kAggregate,       // 聚合约束：AVG/SUM/... OVER (INTERVAL ...)
};

// 执行阶段
enum class ExecutionPhase {
    kPhaseOne,        // 阶段一：逐行过滤（值域 + 行间）
    kPhaseTwo,        // 阶段二：聚合验证（窗口聚合）
};

// 执行模式
enum class ExecutionMode {
    kRowByRow,        // 逐行（仅值域约束）
    kStatefulBatch,   // batch 有状态（含行间约束）
    kTwoPhase,        // 两阶段（含聚合约束）
};

// 单个约束的分类结果
struct ConstraintClassification {
    std::string constraint_name;
    ConstraintType type;
    ExecutionPhase phase;
};

// 整体分类结果
struct ClassificationResult {
    std::vector<ConstraintClassification> classifications;
    ExecutionMode execution_mode;
    int value_range_count;
    int inter_row_count;
    int aggregate_count;

    // 辅助查询
    bool has_inter_row() const { return inter_row_count > 0; }
    bool has_aggregate() const { return aggregate_count > 0; }
    std::vector<ConstraintClassification> phase_one_constraints() const;
    std::vector<ConstraintClassification> phase_two_constraints() const;
};

// 约束分类器
class ConstraintClassifier {
public:
    // 编译时分类：从约束定义确定类型和阶段
    Result<ClassificationResult> classify(
        const std::vector<ConstraintDef>& constraints,
        const Schema& schema);

    // 单个约束分类
    Result<ConstraintType> classify_single(
        const ConstraintDef& constraint,
        const Schema& schema);

    // 执行模式推导
    ExecutionMode derive_execution_mode(
        const ClassificationResult& classification);

    // Explain
    ExplainInfo explain(const ClassificationResult& result) const;

private:
    // 分类规则：
    // - BETWEEN/MIN/MAX → kValueRange, kPhaseOne
    // - [t]/[t-1] → kInterRow, kPhaseOne
    // - AVG/SUM/... OVER → kAggregate, kPhaseTwo
    //
    // 执行模式推导：
    // - 有聚合 → kTwoPhase
    // - 有行间但无聚合 → kStatefulBatch
    // - 仅值域 → kRowByRow
};

}  // namespace synthgen::engine::router
```

### 2.3 错误处理

```cpp
enum class ClassifierErrorCode {
    kEmptyConstraints,               // 空约束列表
    kDuplicateConstraint,            // 约束名重复
    kUnsupportedConstraintType,      // 不支持的约束类型
    kClassificationError,            // 分类结果与实际约束不匹配
    kConflictingConstraints,         // 冲突约束（如同列既有行间又有聚合，且语义矛盾）
    kOrderColumnRequired,            // 行间约束要求 ORDER 列
    kDatetimeColumnRequired,         // 聚合约束要求 ORDER 列为 DATETIME
};
```

---

## 三、Unit L 验收标准

### 3.1 功能验收

- [ ] 值域约束正确分类为 kValueRange + kPhaseOne
- [ ] 行间约束正确分类为 kInterRow + kPhaseOne
- [ ] 聚合约束正确分类为 kAggregate + kPhaseTwo
- [ ] 执行模式推导正确：
  - 仅值域 → kRowByRow
  - 含行间 → kStatefulBatch
  - 含聚合 → kTwoPhase
- [ ] 分类结果计数正确
- [ ] phase_one_constraints() 和 phase_two_constraints() 过滤正确

### 3.2 脚手架验收

- [ ] ConstraintClassifier 提供 explain() 方法
- [ ] 分类结果写入 Trace span

### 3.3 错误测试验收

- [ ] 空约束列表返回 kEmptyConstraints
- [ ] 约束名重复返回 kDuplicateConstraint
- [ ] 不支持的约束类型返回 kUnsupportedConstraintType
- [ ] 行间约束但 Schema 无 ORDER 列返回 kOrderColumnRequired
- [ ] 聚合约束但 ORDER 列非 DATETIME 返回 kDatetimeColumnRequired
- [ ] 混合约束中分类错误返回 kClassificationError

### 3.4 边界条件测试

- [ ] 仅值域约束（1个）
- [ ] 仅值域约束（10个）
- [ ] 值域 + 行间混合
- [ ] 值域 + 聚合混合
- [ ] 值域 + 行间 + 聚合混合
- [ ] 单个行间约束
- [ ] 单个聚合约束
- [ ] 大量约束（50+）

### 3.5 测试验收

- [ ] 单元测试覆盖：分类、推导、错误路径
- [ ] 错误测试用例占比 ≥ 30%
- [ ] 至少 20 个测试用例
- [ ] CI 自动运行

---

## 四、与后续 Unit 的接口

| 接口 | 消费者 | 用途 |
|------|-------|------|
| `ConstraintClassifier::classify()` | Unit M (路由器) | 路由决策输入 |
| `ClassificationResult` | Unit M, Unit P | 执行模式 + 约束分类 |
| `ExecutionMode` | Unit M (路由器) | 路由器选择退化路径 |
| `ConstraintType` | Unit P (EvidencePackage) | constraint_type_breakdown |
