SynthGen Core v2 Unit L 实施计划：约束分类器
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit L 设计规范 v1.0
估算：1 周
依赖：Parser 扩展

---

## 概述

Unit L 交付约束分类器——执行路由器的前置组件，在编译时确定约束类型和执行模式。

---

## Task 1：约束类型枚举和分类规则

**目标**：定义约束类型、执行阶段、执行模式枚举和分类规则

### Step 1.1：枚举定义

**做什么**：定义 ConstraintType、ExecutionPhase、ExecutionMode 枚举

**产出**：`src/engine/router/constraint_classifier.h`

（定义见 Unit L 设计规范 2.2 节）

**验收**：枚举覆盖所有约束类型和执行模式

### Step 1.2：分类规则实现

**做什么**：实现分类规则

**产出**：`src/engine/router/constraint_classifier.h`, `src/engine/router/constraint_classifier.cpp`

**关键逻辑**：
- BETWEEN/MIN/MAX → kValueRange, kPhaseOne
- [t]/[t-1] 语法 → kInterRow, kPhaseOne
- AVG/SUM/... OVER 语法 → kAggregate, kPhaseTwo
- 有聚合 → kTwoPhase
- 有行间但无聚合 → kStatefulBatch
- 仅值域 → kRowByRow

**验收**：
- [ ] 三种约束类型正确识别
- [ ] 执行阶段标记正确
- [ ] 执行模式推导正确

---

## Task 2：ClassificationResult 和辅助方法

**目标**：实现分类结果和辅助查询方法

### Step 2.1：ClassificationResult 实现

**做什么**：实现分类结果结构和辅助方法

**产出**：`src/engine/router/constraint_classifier.h`（扩展）

```cpp
struct ClassificationResult {
    // ... 字段 ...

    std::vector<ConstraintClassification> phase_one_constraints() const {
        std::vector<ConstraintClassification> result;
        for (const auto& c : classifications) {
            if (c.phase == ExecutionPhase::kPhaseOne) result.push_back(c);
        }
        return result;
    }

    std::vector<ConstraintClassification> phase_two_constraints() const {
        std::vector<ConstraintClassification> result;
        for (const auto& c : classifications) {
            if (c.phase == ExecutionPhase::kPhaseTwo) result.push_back(c);
        }
        return result;
    }
};
```

**验收**：
- [ ] phase_one_constraints() 过滤正确
- [ ] phase_two_constraints() 过滤正确
- [ ] 计数字段正确

### Step 2.2：classify 单约束方法

**做什么**：实现 classify_single 方法

**关键逻辑**：
- 检查 ConstraintDef 类型
- 检查列存在性和类型
- 检查 ORDER 列要求
- 返回 ConstraintType

**验收**：
- [ ] 单约束分类正确
- [ ] 错误情况返回正确 ErrorCode

---

## Task 3：错误处理

**目标**：完善分类器错误处理

### Step 3.1：错误路径实现

**做什么**：实现所有 ErrorCode

**验收**：
- [ ] kEmptyConstraints
- [ ] kDuplicateConstraint
- [ ] kUnsupportedConstraintType
- [ ] kOrderColumnRequired
- [ ] kDatetimeColumnRequired
- [ ] kConflictingConstraints

### Step 3.2：错误测试

**做什么**：编写错误路径测试

**产出**：`tests/unit/constraint_classifier_error_test.cpp`

**测试用例**（至少 6 个）：
- 空约束列表
- 约束名重复
- 不支持的约束类型
- 行间约束但无 ORDER 列
- 聚合约束但非 DATETIME
- 冲突约束

**验收**：6+ 错误测试通过

---

## Task 4：功能测试

**目标**：完整的功能和边界条件测试

### Step 4.1：功能测试

**做什么**：编写分类器功能测试

**产出**：`tests/unit/constraint_classifier_test.cpp`

**测试用例**（至少 14 个）：
- 仅值域约束（1个）
- 仅值域约束（10个）
- 值域+行间混合
- 值域+聚合混合
- 三类混合
- 单个行间约束
- 单个聚合约束
- kRowByRow 推导
- kStatefulBatch 推导
- kTwoPhase 推导
- phase_one 过滤
- phase_two 过滤
- 大量约束（50+）
- 混合约束多次分类幂等

**验收**：14+ 功能测试通过

---

## Task 5：脚手架集成

**目标**：为 ConstraintClassifier 添加 Trace/Explain

### Step 5.1：Explain 接口

**做什么**：为分类器添加 explain() 方法

```cpp
struct ClassifierExplainInfo {
    std::vector<ConstraintClassification> classifications;
    ExecutionMode execution_mode;
    std::string execution_mode_reason;  // 为什么选这个模式
};
```

**验收**：explain() 返回分类详情和模式推导理由

### Step 5.2：Trace span

**做什么**：为分类过程添加 span

**验收**：每次 classify 产生 span

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| Task 1: 分类规则 | 2 | 0.25w | ⬜ |
| Task 2: 结果和辅助 | 2 | 0.25w | ⬜ |
| Task 3: 错误处理 | 2 | 0.125w | ⬜ |
| Task 4: 功能测试 | 1 | 0.125w | ⬜ |
| Task 5: 脚手架 | 2 | 0.25w | ⬜ |
| **合计** | **9** | **1w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| 分类规则与实际引擎行为不一致 | 分类器与引擎共享约束类型定义 |
| v4 新增窗口类型导致分类规则扩展 | 预留 kRows/kSession 窗口类型，v4 时激活 |
| 复杂约束组合的分类冲突 | 明确优先级：聚合 > 行间 > 值域 |
