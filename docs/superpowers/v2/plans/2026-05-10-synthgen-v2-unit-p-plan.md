SynthGen Core v2 Unit P 实施计划：DURING/WHEN + EvidencePackage v2
文档性质：Unit 级实施计划
版本：v1.0
日期：2026-05-10
上游文档：Unit P 设计规范 v1.0
估算：1.5 周
依赖：#12 分类器 + #13 路由器 + #14 后筛选 + #15 审计

---

## 概述

Unit P 包含两个组件：#16 DURING/WHEN 语义（1w）+ #17 EvidencePackage v2（0.5w）。

---

## Part A：#16 DURING/WHEN 语义

### Task A1：Parser 条件约束语法扩展

#### Step A1.1：Token 和 AST 扩展

**做什么**：添加 DURING/WHEN/THEN Token 和 AST 节点

**产出**：`src/parser/token.h`, `src/parser/ast.h`（扩展）

**验收**：条件约束语法可解析

#### Step A1.2：条件约束解析

**做什么**：实现 DURING/WHEN 语法解析

**验收**：
- [ ] DURING column = value 正确解析
- [ ] WHEN condition THEN constraint 正确解析
- [ ] 错误语法返回正确错误码

#### Step A1.3：解析测试

**产出**：`tests/unit/conditional_parser_test.cpp`

**验收**：6+ 解析测试通过

---

### Task A2：条件约束引擎

#### Step A2.1：条件约束应用

**做什么**：实现 apply() 方法

**产出**：`src/engine/constraint/conditional_engine.h`, `src/engine/constraint/conditional_engine.cpp`

**关键逻辑**：
- DURING：检查行中 during_column 是否等于 during_value
- WHEN：评估 when_condition 表达式
- 条件满足时应用 conditional_constraints

**验收**：
- [ ] DURING 条件正确
- [ ] WHEN 条件正确
- [ ] 条件不满足时跳过约束

#### Step A2.2：非矩形约束域生成

**做什么**：实现拒绝采样/MCMC

**关键逻辑**：
- 拒绝采样：在值域范围内采样 → 检查条件 → 拒绝不满足的
- MCMC：拒绝采样不收敛时的后备方案

**验收**：
- [ ] 拒绝采样正确
- [ ] MCMC 后备方案

#### Step A2.3：引擎测试

**产出**：`tests/unit/conditional_engine_test.cpp`

**验收**：10+ 引擎测试通过

---

## Part B：#17 EvidencePackage v2

### Task B1：v2 字段扩展

#### Step B1.1：EvidencePackageV2 结构体

**做什么**：扩展 EvidencePackage 为 v2

**产出**：`src/engine/evidence/evidence_package.h`（扩展）

**验收**：
- [ ] v2 新增字段定义完整
- [ ] v1 字段保持兼容

#### Step B1.2：构建器扩展

**做什么**：扩展 EvidencePackageBuilder 支持 v2 字段

**产出**：`src/engine/evidence/evidence_package_builder.cpp`（扩展）

**验收**：
- [ ] statistical_fidelity 数据驱动时填充
- [ ] constraint_type_breakdown 三类计数
- [ ] generator_identity 每条路径
- [ ] audit_immutability = "verified"
- [ ] post_filter_info 后筛选时填充

#### Step B1.3：Schema 验证

**做什么**：更新 EvidencePackage Schema 验证

**验收**：
- [ ] v2 Schema 验证正确
- [ ] v1 Schema 向下兼容

---

### Task B2：测试

#### Step B2.1：EvidencePackage v2 测试

**产出**：`tests/unit/evidence_package_v2_test.cpp`

**测试用例**（至少 10 个）：
- v2 字段填充
- statistical_fidelity 数据驱动时
- statistical_fidelity 无数据引擎时
- constraint_type_breakdown 正确
- generator_identity 各路径
- audit_immutability verified
- post_filter_info 后筛选时
- v1 兼容性
- Schema 验证
- 序列化/反序列化

**验收**：10+ 测试通过

---

## 进度追踪

| Task | 步骤数 | 估算 | 状态 |
|------|--------|------|------|
| A1: Parser 扩展 | 3 | 0.25w | ⬜ |
| A2: 条件约束引擎 | 3 | 0.75w | ⬜ |
| **#16 小计** | **6** | **1w** | — |
| B1: v2 字段 | 3 | 0.25w | ⬜ |
| B2: 测试 | 1 | 0.25w | ⬜ |
| **#17 小计** | **4** | **0.5w** | — |
| **合计** | **10** | **1.5w** | — |

---

## 风险

| 风险 | 缓解 |
|------|------|
| DURING/WHEN 条件解析复杂度高 | v2 仅支持简单等值条件和比较条件 |
| 拒绝采样在高维条件空间效率低 | 限制条件维度，高维时 MCMC 后备 |
| EvidencePackage v2/v1 兼容性问题 | schema_version 区分，v1 字段保持不变 |
